"""Transport management tests — validate transport operations via CLI.

Strong assertions: where the existing test only checked `isinstance(data, list)`,
we now bootstrap a known transport at class setup and require the list output
to contain it. This catches regressions where the command returns an empty
array even though transports exist (the failure mode of issue #9).
"""

import re

import pytest


TRANSPORT_PATTERN = re.compile(r"^[A-Z0-9]{3}[A-Z]\d{6}$")


@pytest.fixture(scope="class")
def bootstrap_transport(cli):
    """Create a transport at class setup so the list test has a known target.

    Best-effort release on teardown — the transport may already have been
    consumed by a sibling test, so we don't fail if release returns non-zero.
    """
    data = cli.run_ok(
        "transport", "create",
        "--desc", "pytest bootstrap (test_03_transport)",
        "--package", "$TMP",
    )
    number = data["transport_number"]
    assert TRANSPORT_PATTERN.match(number), \
        f"transport create returned an invalid number: {number}"

    yield number

    # Best-effort release: not all systems allow immediate release; we don't
    # want a teardown failure to mask the real test result.
    cli.run("transport", "release", number)


@pytest.mark.transport
class TestTransport:
    """Read-side tests — exercise list/has-fields against a known transport."""

    def test_list_contains_bootstrap_transport(self, cli, bootstrap_transport):
        """transport list must include the transport we just created.

        This is the assertion that would have caught issue #9: previously
        the test only asserted `isinstance(data, list)`, so an empty list
        passed silently while the command was broken.
        """
        data = cli.run_ok("transport", "list", "--user", "DEVELOPER")
        assert isinstance(data, list), \
            f"Expected JSON array, got {type(data).__name__}"
        assert len(data) > 0, \
            "transport list returned an empty array but at least one " \
            f"transport ({bootstrap_transport}) was just created"

        numbers = [t.get("number") for t in data]
        assert bootstrap_transport in numbers, (
            f"Bootstrap transport {bootstrap_transport} missing from "
            f"transport list. Got: {numbers}"
        )

    def test_list_entry_has_expected_fields(self, cli, bootstrap_transport):
        """Every transport entry must populate number, description, owner, status.

        Catches "structurally OK but parser missing fields" regressions.
        """
        data = cli.run_ok("transport", "list", "--user", "DEVELOPER")
        entry = next(
            (t for t in data if t.get("number") == bootstrap_transport),
            None,
        )
        assert entry is not None, "bootstrap transport not in list"

        # Required fields — present and populated.
        for field in ("number", "owner", "status"):
            assert entry.get(field), f"Field '{field}' missing or empty: {entry}"

        assert entry["number"] == bootstrap_transport
        assert entry["owner"] == "DEVELOPER"
        assert entry["status"] in ("modifiable", "released", "locked"), \
            f"Unexpected status: {entry['status']!r}"
        assert entry["description"] == "pytest bootstrap (test_03_transport)", \
            "description was not round-tripped through create -> list"

    def test_list_human_readable_contains_transport(self, cli, bootstrap_transport):
        """Non-JSON output must render the bootstrap transport too.

        Guards the OutputFormatter table path, which is a separate code
        path from the JSON serializer.
        """
        result = cli.run_no_json("transport", "list", "--user", "DEVELOPER")
        assert result.returncode == 0
        assert bootstrap_transport in result.stdout, (
            f"Bootstrap transport {bootstrap_transport} missing from "
            f"human-readable output:\n{result.stdout}"
        )


@pytest.mark.transport
class TestTransportMutations:
    """Create and release tests — keep these isolated from the read tests."""

    def test_create_returns_valid_number(self, cli):
        """transport create returns a transport number matching the SAP format."""
        data = cli.run_ok(
            "transport", "create",
            "--desc", "pytest create-only test",
            "--package", "$TMP",
        )
        assert "transport_number" in data
        assert TRANSPORT_PATTERN.match(data["transport_number"]), \
            f"Invalid transport number: {data['transport_number']}"
        # Best-effort release so we don't accumulate transports on the trial.
        cli.run("transport", "release", data["transport_number"])

    def test_create_then_release(self, cli):
        """Create then release; tolerate the "cannot release immediately" case."""
        create_data = cli.run_ok(
            "transport", "create",
            "--desc", "pytest release test",
            "--package", "$TMP",
        )
        number = create_data["transport_number"]

        result = cli.run("transport", "release", number)
        # Release may succeed (0) or fail with transport error (9) if the
        # system does not allow immediate release of empty transports.
        assert result.returncode in (0, 9), \
            f"Unexpected exit code: {result.returncode}\nstderr: {result.stderr}"

    def test_create_validates_required_flags(self, cli):
        """Missing --desc or --package is a validation error, not a crash."""
        # Missing --desc
        result = cli.run("transport", "create", "--package", "$TMP")
        assert result.returncode != 0
        assert "desc" in result.stderr.lower() or "desc" in result.stdout.lower()

        # Missing --package
        result = cli.run("transport", "create", "--desc", "no package")
        assert result.returncode != 0
        assert "package" in result.stderr.lower() or "package" in result.stdout.lower()
