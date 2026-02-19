"""BW export command integration tests — bw export <infoarea>."""

import json
import os
import tempfile

import pytest


@pytest.mark.bw
class TestBwExport:
    """Integration tests for 'erpl-adt bw export <infoarea>'."""

    @pytest.fixture(scope="class")
    def known_infoarea(self, cli, bw_has_search):
        """Find a known InfoArea to export."""
        data = cli.run_ok("bw", "search", "*", "--type", "AREA", "--max", "5")
        if not data:
            pytest.skip("No AREA objects found on this system")
        # Prefer the demo infoarea if present
        for item in data:
            if "0D_NW_DEMO" in item.get("name", ""):
                return item["name"]
        return data[0]["name"]

    # -----------------------------------------------------------------------
    # Catalog JSON shape (default)
    # -----------------------------------------------------------------------

    def test_export_catalog_json(self, cli, known_infoarea):
        """Catalog export has required contract, objects, and dataflow sections."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--no-lineage", "--no-queries")
        assert data.get("contract") == "bw.infoarea.export", (
            f"Expected contract='bw.infoarea.export', got {data.get('contract')!r}"
        )
        assert data.get("infoarea") == known_infoarea
        assert "objects" in data, "Missing 'objects' key"
        assert "dataflow" in data, "Missing 'dataflow' key"
        assert "schema_version" in data
        assert "exported_at" in data

    def test_export_objects_have_types(self, cli, known_infoarea):
        """Each exported object has name and type fields."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--no-lineage", "--no-queries")
        for obj in data.get("objects", []):
            assert "name" in obj, f"Object missing 'name': {obj}"
            assert "type" in obj, f"Object missing 'type': {obj}"

    # -----------------------------------------------------------------------
    # Mermaid output
    # -----------------------------------------------------------------------

    def test_export_mermaid(self, cli, known_infoarea):
        """Mermaid output starts with 'graph LR'."""
        result = cli.run_no_json("bw", "export", known_infoarea, "--mermaid",
                                 "--no-lineage", "--no-queries")
        assert result.returncode == 0, f"bw export --mermaid failed: {result.stderr}"
        output = result.stdout
        assert "graph LR" in output, f"Expected 'graph LR' in Mermaid output:\n{output}"

    # -----------------------------------------------------------------------
    # OpenMetadata shape
    # -----------------------------------------------------------------------

    def test_export_openmetadata_shape(self, cli, known_infoarea):
        """OpenMetadata shape includes serviceType, tables, and lineage arrays."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--shape", "openmetadata",
                          "--service-name", "erpl_adt",
                          "--system-id", "A4H",
                          "--no-lineage", "--no-queries")
        assert data.get("serviceType") == "SapBw", (
            f"Expected serviceType='SapBw', got {data.get('serviceType')!r}"
        )
        assert "tables" in data, "Missing 'tables' key in openmetadata shape"
        assert "lineage" in data, "Missing 'lineage' key in openmetadata shape"
        assert data.get("serviceName") == "erpl_adt"

    # -----------------------------------------------------------------------
    # Types filter
    # -----------------------------------------------------------------------

    def test_export_types_filter(self, cli, known_infoarea):
        """--types ADSO only returns ADSO objects (or zero objects if none exist)."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--types", "ADSO",
                          "--no-lineage", "--no-queries")
        assert "objects" in data
        for obj in data["objects"]:
            assert obj["type"] == "ADSO", (
                f"Expected only ADSO objects but got type {obj['type']!r}"
            )

    # -----------------------------------------------------------------------
    # --no-lineage flag
    # -----------------------------------------------------------------------

    def test_export_no_lineage(self, cli, known_infoarea):
        """With --no-lineage, DTP objects must not have a 'lineage' key."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--types", "DTPA",
                          "--no-lineage")
        for obj in data.get("objects", []):
            assert "lineage" not in obj, (
                f"DTP object {obj['name']} has unexpected 'lineage' key "
                f"when --no-lineage was requested"
            )

    # -----------------------------------------------------------------------
    # --out-dir flag
    # -----------------------------------------------------------------------

    def test_export_out_dir(self, cli, known_infoarea):
        """--out-dir creates catalog JSON and Mermaid .mmd files on disk."""
        with tempfile.TemporaryDirectory() as tmpdir:
            result = cli.run_no_json(
                "bw", "export", known_infoarea,
                "--out-dir", tmpdir,
                "--no-lineage", "--no-queries",
            )
            assert result.returncode == 0, f"export --out-dir failed: {result.stderr}"
            catalog_path = os.path.join(tmpdir, known_infoarea + "_catalog.json")
            mmd_path = os.path.join(tmpdir, known_infoarea + "_dataflow.mmd")
            assert os.path.exists(catalog_path), f"Missing catalog file: {catalog_path}"
            assert os.path.exists(mmd_path), f"Missing mermaid file: {mmd_path}"
            # Verify catalog is valid JSON with expected contract
            with open(catalog_path) as f:
                data = json.load(f)
            assert data.get("contract") == "bw.infoarea.export"
            # Verify mermaid has graph LR
            with open(mmd_path) as f:
                mmd_content = f.read()
            assert "graph LR" in mmd_content

    # -----------------------------------------------------------------------
    # Error handling
    # -----------------------------------------------------------------------

    def test_export_includes_iobj(self, cli, known_infoarea):
        """Export should include IOBJ objects (via search supplement)."""
        data = cli.run_ok("bw", "export", known_infoarea, "--no-lineage", "--no-queries")
        types = {obj["type"] for obj in data.get("objects", [])}
        assert len(data["objects"]) > 0, "Export returned no objects"
        if "IOBJ" not in types:
            pytest.xfail(
                f"No IOBJ found in {known_infoarea} — infoarea may not have IOBJs "
                f"or infoArea search parameter is not supported by this system"
            )

    def test_export_no_search_flag(self, cli, known_infoarea):
        """--no-search disables search supplement (BFS tree only)."""
        data = cli.run_ok("bw", "export", known_infoarea,
                          "--no-search", "--no-lineage", "--no-queries")
        assert data.get("contract") == "bw.infoarea.export"
        # Provenance should not contain BwSearchObjects entry
        for p in data.get("provenance", []):
            assert p.get("operation") != "BwSearchObjects", (
                "BwSearchObjects in provenance despite --no-search flag"
            )

    def test_export_missing_arg_exits_nonzero(self, cli):
        """Missing infoarea argument must exit non-zero."""
        result = cli.run_fail("bw", "export")
        assert result.returncode != 0
