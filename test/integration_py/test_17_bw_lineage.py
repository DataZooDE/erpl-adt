"""BW lineage command tests — read-adso, read-trfn, read-dtp, read-rsds, read-query, read-dmod, lineage."""

import json
import shutil
import subprocess
import tempfile

import pytest


# ===========================================================================
# BW Lineage: read-adso, read-trfn, read-dtp, read-rsds, read-query, read-dmod
# ===========================================================================

@pytest.mark.bw
class TestBwLineage:

    @pytest.fixture(scope="class")
    def known_adso(self, cli, bw_has_search):
        """Find a known ADSO for lineage tests."""
        data = cli.run_ok("bw", "search", "*", "--max", "1",
                          "--type", "ADSO")
        if not data:
            pytest.skip("No ADSO found")
        return data[0]["name"]

    @pytest.fixture(scope="class")
    def known_trfn(self, cli, bw_has_search):
        """Find a known transformation that is actually readable."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "TRFN")
        if not data:
            pytest.skip("No TRFN found")
        # Search index may be stale — probe read endpoint
        for r in data:
            result = cli.run("bw", "read-trfn", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable TRFN found (search results return 404)")

    @pytest.fixture(scope="class")
    def known_dtp(self, cli, bw_has_search):
        """Find a known DTP that is actually readable."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "DTPA")
        if not data:
            pytest.skip("No DTPA found")
        # Search index may be stale — probe read endpoint
        for r in data:
            result = cli.run("bw", "read-dtp", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable DTPA found (search results return 404)")

    @pytest.fixture(scope="class")
    def known_rsds(self, cli, bw_has_search):
        """Find a known RSDS with source-system info from URI."""
        data = cli.run_ok("bw", "search", "*", "--max", "10",
                          "--type", "RSDS")
        if not data:
            pytest.skip("No RSDS found")
        for r in data:
            uri = r.get("uri", "")
            parts = [p for p in uri.split("/") if p]
            if "rsds" not in parts:
                continue
            idx = parts.index("rsds")
            if idx + 3 >= len(parts):
                continue
            name = parts[idx + 1]
            logsys = parts[idx + 2]
            probe = cli.run("bw", "read-rsds", name, "--source-system", logsys)
            if probe.returncode == 0:
                return (name, logsys)
        pytest.skip("No readable RSDS found")

    @pytest.fixture(scope="class")
    def known_query(self, cli, bw_has_search):
        """Find a readable BW query component."""
        result = cli.run("bw", "search", "*", "--max", "10", "--type", "QUERY")
        if result.returncode != 0:
            pytest.skip("QUERY search not supported on this system")
        data = json.loads(result.stdout)
        if not data:
            pytest.skip("No QUERY found")
        for r in data:
            result = cli.run("bw", "read-query", "query", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable QUERY found")

    @pytest.fixture(scope="class")
    def known_dmod(self, cli, bw_has_search):
        """Find a readable BW dataflow (DMOD)."""
        data = cli.run_ok("bw", "search", "*", "--max", "10", "--type", "DMOD")
        if not data:
            pytest.skip("No DMOD found")
        for r in data:
            result = cli.run("bw", "read-dmod", r["name"])
            if result.returncode == 0:
                return r["name"]
        pytest.skip("No readable DMOD found")

    # --- read-adso tests ---

    def test_read_adso_returns_fields(self, cli, known_adso):
        """read-adso returns structured field list."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        assert "name" in data
        assert data["name"] == known_adso
        assert "fields" in data
        assert isinstance(data["fields"], list)

    def test_read_adso_field_has_properties(self, cli, known_adso):
        """Each ADSO field has name, data_type, key."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        if not data["fields"]:
            pytest.skip("ADSO has no fields")
        f = data["fields"][0]
        assert "name" in f
        assert "data_type" in f
        assert "key" in f

    def test_read_adso_has_package(self, cli, known_adso):
        """read-adso returns package name."""
        data = cli.run_ok("bw", "read-adso", known_adso)
        assert "package" in data

    def test_read_adso_human_readable(self, cli, known_adso):
        """read-adso without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-adso", known_adso)
        assert result.returncode == 0
        assert known_adso in result.stdout

    def test_read_adso_nonexistent(self, cli, bw_has_search):
        """read-adso for nonexistent ADSO returns error."""
        result = cli.run("bw", "read-adso", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    def test_read_adso_missing_args(self, cli):
        """bw read-adso without name fails."""
        result = cli.run("bw", "read-adso")
        assert result.returncode != 0

    # --- read-trfn tests ---

    def test_read_trfn_returns_source_target(self, cli, known_trfn):
        """read-trfn returns source and target information."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        assert "name" in data
        assert data["name"] == known_trfn
        assert "source_name" in data
        assert "source_type" in data
        assert "target_name" in data
        assert "target_type" in data

    def test_read_trfn_has_fields_and_rules(self, cli, known_trfn):
        """read-trfn returns field lists and rules."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        assert "source_fields" in data
        assert "target_fields" in data
        assert "rules" in data
        assert isinstance(data["source_fields"], list)
        assert isinstance(data["target_fields"], list)
        assert isinstance(data["rules"], list)

    def test_read_trfn_rule_has_properties(self, cli, known_trfn):
        """Transformation rules have expected properties."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        if not data["rules"]:
            pytest.skip("Transformation has no rules")
        r = data["rules"][0]
        assert "source_field" in r
        assert "target_field" in r
        assert "rule_type" in r

    def test_read_trfn_includes_step_contract_fields(self, cli, known_trfn):
        """read-trfn exposes expanded step/group contract fields."""
        data = cli.run_ok("bw", "read-trfn", known_trfn)
        assert "start_routine" in data
        assert "end_routine" in data
        assert "expert_routine" in data
        assert "hana_runtime" in data
        if data["rules"]:
            r = data["rules"][0]
            assert "source_fields" in r
            assert "target_fields" in r
            assert "group_id" in r
            assert "group_type" in r
            assert "step_attributes" in r

    def test_read_trfn_human_readable(self, cli, known_trfn):
        """read-trfn without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-trfn", known_trfn)
        assert result.returncode == 0
        assert known_trfn in result.stdout

    def test_read_trfn_nonexistent(self, cli, bw_has_search):
        """read-trfn for nonexistent TRFN returns error."""
        result = cli.run("bw", "read-trfn", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    def test_read_trfn_missing_args(self, cli):
        """bw read-trfn without name fails."""
        result = cli.run("bw", "read-trfn")
        assert result.returncode != 0

    # --- read-dtp tests ---

    def test_read_dtp_returns_connection(self, cli, known_dtp):
        """read-dtp returns source/target connection."""
        data = cli.run_ok("bw", "read-dtp", known_dtp)
        assert "name" in data
        assert data["name"] == known_dtp
        assert "source_name" in data
        assert "source_type" in data
        assert "target_name" in data
        assert "target_type" in data

    def test_read_dtp_has_source_system(self, cli, known_dtp):
        """read-dtp includes source_system field."""
        data = cli.run_ok("bw", "read-dtp", known_dtp)
        assert "source_system" in data

    def test_read_dtp_includes_execution_contract_fields(self, cli, known_dtp):
        """read-dtp exposes execution/filter/program flow contract fields."""
        data = cli.run_ok("bw", "read-dtp", known_dtp)
        assert "type" in data
        assert "request_selection_mode" in data
        assert "extraction_settings" in data
        assert "execution_settings" in data
        assert "runtime_properties" in data
        assert "error_handling" in data
        assert "dtp_execution" in data
        assert "semantic_group_fields" in data
        assert "filter_fields" in data
        assert "program_flow" in data

    def test_read_dtp_human_readable(self, cli, known_dtp):
        """read-dtp without --json produces human-readable output."""
        result = cli.run_no_json("bw", "read-dtp", known_dtp)
        assert result.returncode == 0
        assert known_dtp in result.stdout

    def test_read_dtp_nonexistent(self, cli, bw_has_search):
        """read-dtp for nonexistent DTP returns error."""
        result = cli.run("bw", "read-dtp", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode == 2

    def test_read_dtp_missing_args(self, cli):
        """bw read-dtp without name fails."""
        result = cli.run("bw", "read-dtp")
        assert result.returncode != 0

    # --- read-rsds tests ---

    def test_read_rsds_returns_fields(self, cli, known_rsds):
        """read-rsds returns structured field list."""
        name, logsys = known_rsds
        data = cli.run_ok("bw", "read-rsds", name, "--source-system", logsys)
        assert data["name"] == name
        assert data["source_system"] == logsys
        assert isinstance(data.get("fields"), list)

    def test_read_rsds_nonexistent(self, cli, bw_has_search):
        """read-rsds for nonexistent RSDS returns error."""
        result = cli.run("bw", "read-rsds", "ZZZZZ_NONEXISTENT_99999",
                         "--source-system", "ECLCLNT100")
        assert result.returncode == 2

    def test_read_rsds_missing_args(self, cli):
        """bw read-rsds without name/source-system fails."""
        result = cli.run("bw", "read-rsds")
        assert result.returncode != 0

    # --- read-query tests ---

    def test_read_query_returns_component_contract(self, cli, known_query):
        """read-query returns query component contract."""
        data = cli.run_ok("bw", "read-query", "query", known_query)
        assert data["name"] == known_query
        assert data["component_type"] == "QUERY"
        assert data["schema_version"] == "1.0"
        assert isinstance(data.get("root_node_id"), str)
        assert data["root_node_id"]
        assert "metadata" in data
        assert isinstance(data["metadata"], dict)
        assert data["metadata"].get("name") == known_query
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
        assert any(n.get("id") == data["root_node_id"] for n in data["nodes"])
        assert all(isinstance(n.get("id"), str) for n in data["nodes"])
        assert all(isinstance(e.get("from"), str) and isinstance(e.get("to"), str)
                   for e in data["edges"])
        assert "references" in data
        assert isinstance(data["references"], list)

    def test_read_query_default_mermaid_output(self, cli):
        """read-query without --json defaults to Mermaid graph."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run_no_json("bw", "read-query", query_name)
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")
        out = result.stdout
        assert out.startswith("graph TD")
        assert "subgraph Query" in out
        assert "subgraph References" in out
        assert "classDef query" in out
        assert query_name in out
        assert "VARIABLE: 0D_NW_ACTCMON" in out

    def test_read_query_detailed_mermaid_layout(self, cli):
        """read-query supports detailed Mermaid layout and LR direction."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run_no_json(
            "bw", "read-query", query_name, "--layout=detailed", "--direction=LR"
        )
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")
        out = result.stdout
        assert out.startswith("graph LR")
        assert "subgraph Query" in out
        assert "subgraph References" in out
        assert "classDef query" in out

    def test_read_query_invalid_layout_fails(self, cli):
        """read-query with unsupported layout fails with validation error."""
        result = cli.run_no_json("bw", "read-query", "0D_FC_NW_C01_Q0007", "--layout=wide")
        assert result.returncode == 99
        assert "invalid --layout" in result.stderr.lower()

    def test_read_query_invalid_direction_fails(self, cli):
        """read-query with unsupported direction fails with validation error."""
        result = cli.run_no_json("bw", "read-query", "0D_FC_NW_C01_Q0007", "--direction=BT")
        assert result.returncode == 99
        assert "invalid --direction" in result.stderr.lower()

    def test_read_query_json_reduction_metadata(self, cli):
        """read-query JSON includes explicit reduction metadata when requested."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run(
            "bw", "read-query", "query", query_name,
            "--max-nodes-per-role=1", "--focus-role=filter"
        )
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")
        data = json.loads(result.stdout.strip())
        reduction = data.get("reduction", {})
        assert reduction.get("applied") is True
        assert reduction.get("focus_role") == "filter"
        assert reduction.get("max_nodes_per_role") == 1
        assert isinstance(reduction.get("summaries"), list)

    def test_read_query_catalog_json_shape(self, cli):
        """read-query supports catalog-oriented flat JSON contract."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run("bw", "read-query", "query", query_name, "--json-shape=catalog")
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")
        data = json.loads(result.stdout.strip())
        assert data.get("contract") == "bw.query.catalog"
        assert data.get("schema_version") == "2.0"
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
        if data["nodes"]:
            n = data["nodes"][0]
            assert "node_id" in n
            assert "business_key" in n
            assert "object_type" in n
            assert "object_name" in n
            assert "source_component_type" in n
            assert "source_component_name" in n
        metrics = data.get("metrics", {})
        assert isinstance(metrics.get("node_count"), int)
        assert isinstance(metrics.get("edge_count"), int)
        assert isinstance(metrics.get("ergonomics_flags"), list)

    def test_read_query_with_upstream_lineage_composition(self, cli, known_dtp):
        """read-query can compose upstream DTP lineage into one graph payload."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run(
            "bw", "read-query", "query", query_name,
            "--upstream-dtp", known_dtp,
            "--upstream-no-xref",
            "--json-shape=catalog"
        )
        if result.returncode != 0:
            pytest.skip("Query or DTP lineage not available in this landscape")
        data = json.loads(result.stdout.strip())
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
        assert any(n.get("object_type") == "DTPA" and n.get("object_name") == known_dtp
                   for n in data["nodes"])
        assert any(e.get("edge_type") == "upstream_bridge" for e in data["edges"])

    def test_read_query_invalid_focus_role_fails(self, cli):
        """read-query with unsupported focus role fails with validation error."""
        result = cli.run_no_json(
            "bw", "read-query", "0D_FC_NW_C01_Q0007", "--focus-role=everything"
        )
        assert result.returncode == 99
        assert "invalid --focus-role" in result.stderr.lower()

    def test_read_query_invalid_max_nodes_per_role_fails(self, cli):
        """read-query with non-positive max nodes per role fails with validation error."""
        result = cli.run_no_json(
            "bw", "read-query", "0D_FC_NW_C01_Q0007", "--max-nodes-per-role=0"
        )
        assert result.returncode == 99
        assert "invalid --max-nodes-per-role" in result.stderr.lower()

    def test_read_query_invalid_json_shape_fails(self, cli):
        """read-query with unsupported json shape fails with validation error."""
        result = cli.run_no_json(
            "bw", "read-query", "0D_FC_NW_C01_Q0007", "--json-shape=flat"
        )
        assert result.returncode == 99
        assert "invalid --json-shape" in result.stderr.lower()

    def test_read_query_upstream_dtp_for_non_query_fails(self, cli):
        """upstream composition is only supported for query component reads."""
        result = cli.run_no_json(
            "bw", "read-query", "variable", "0D_NW_ACTCMON", "--upstream-dtp=DTP_ZSALES"
        )
        assert result.returncode == 99
        assert "only supported for query components" in result.stderr.lower()

    def test_read_query_mermaid_renderable_with_mmdc(self, cli):
        """Mermaid output can be rendered to SVG with mmdc when installed."""
        if shutil.which("mmdc") is None:
            pytest.skip("mmdc not installed")

        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run_no_json("bw", "read-query", query_name, "--layout=detailed")
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")

        with tempfile.TemporaryDirectory() as tmpdir:
            mmd_path = f"{tmpdir}/query.mmd"
            svg_path = f"{tmpdir}/query.svg"
            with open(mmd_path, "w", encoding="utf-8") as f:
                f.write(result.stdout)
            render = subprocess.run(
                ["mmdc", "-i", mmd_path, "-o", svg_path],
                capture_output=True,
                text=True,
                timeout=30,
            )
            assert render.returncode == 0, render.stderr
            with open(svg_path, "r", encoding="utf-8") as f:
                svg = f.read()
            assert "<svg" in svg

    def test_read_query_known_demo_query_parses_live_shape(self, cli):
        """Known SAP demo query returns parsed description/provider/references."""
        query_name = "0D_FC_NW_C01_Q0007"
        result = cli.run("bw", "read-query", "query", query_name)
        if result.returncode != 0:
            pytest.skip("Known demo query not available in this landscape")
        data = json.loads(result.stdout.strip())
        assert data["name"] == query_name
        assert data["component_type"] == "QUERY"
        assert data.get("description")
        assert data.get("info_provider")
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
        assert len(data["nodes"]) > 1
        assert len(data["edges"]) > 0
        assert isinstance(data.get("references"), list)
        assert len(data["references"]) > 0
        assert any(
            r.get("type") == "VARIABLE" and r.get("name") == "0D_NW_ACTCMON"
            for r in data["references"]
        )

    def test_read_query_nonexistent_returns_not_found(self, cli):
        """read-query for nonexistent object returns a clear not-found error."""
        result = cli.run("bw", "read-query", "query", "ZZZZZ_NONEXISTENT_Q_99999")
        assert result.returncode == 2
        assert "not found" in result.stderr.lower()

    def test_read_query_invalid_component_type(self, cli):
        """bw read-query with unsupported component type fails."""
        result = cli.run("bw", "read-query", "unknown", "ZQ_TEST")
        assert result.returncode != 0
        assert "unsupported query component type" in result.stderr.lower()

    def test_read_query_invalid_format(self, cli):
        """bw read-query with unsupported --format fails."""
        result = cli.run("bw", "read-query", "0D_FC_NW_C01_Q0007", "--format=dot")
        assert result.returncode != 0
        assert "invalid --format" in result.stderr.lower()

    # --- read-dmod tests ---

    def test_read_dmod_returns_topology_contract(self, cli, known_dmod):
        """read-dmod returns dataflow topology contract."""
        data = cli.run_ok("bw", "read-dmod", known_dmod)
        assert data["name"] == known_dmod
        assert "nodes" in data
        assert "connections" in data
        assert isinstance(data["nodes"], list)
        assert isinstance(data["connections"], list)

    # --- lineage tests ---

    def test_lineage_graph_contract(self, cli, known_dtp):
        """bw lineage returns canonical graph contract."""
        result = cli.run("bw", "lineage", known_dtp)
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("bw lineage prerequisites not available")
            pytest.fail(f"bw lineage failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert data.get("schema_version") == "1.0"
        assert data.get("root", {}).get("type") == "DTPA"
        assert data.get("root", {}).get("name") == known_dtp
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
        assert isinstance(data.get("provenance"), list)
        assert isinstance(data.get("warnings"), list)

    def test_lineage_graph_no_xref_contract(self, cli, known_dtp):
        """bw lineage --no-xref still returns graph contract."""
        result = cli.run("bw", "lineage", known_dtp, "--no-xref")
        if result.returncode != 0:
            stderr = result.stderr.strip().lower()
            if "not activated" in stderr or "not implemented" in stderr:
                pytest.skip("bw lineage prerequisites not available")
            pytest.fail(f"bw lineage --no-xref failed: {result.stderr.strip()}")
        data = json.loads(result.stdout.strip())
        assert isinstance(data.get("nodes"), list)
        assert isinstance(data.get("edges"), list)
