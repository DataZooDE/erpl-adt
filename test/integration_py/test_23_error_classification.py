"""HTTP 403 must be reported as what it actually is.

Every 403 used to be classified as `csrf_token` with the message "Forbidden —
CSRF token may be invalid", so an unactivated service and a lock conflict both
blamed a stale token. A 403 is only a token problem when nothing else explains
it.
"""

import json

import pytest

from adt_proxy import Sap740Proxy
from cli_runner import CliRunner

pytestmark = pytest.mark.errors


@pytest.fixture(scope="module")
def cli_bw_off(sap_config, cli):
    """CLI pointed at the live system with /sap/bw/* answering 403.

    Emulates a system whose BW ICF nodes were never activated, which is what
    the trial container looks like after a restart.
    """
    with Sap740Proxy(sap_config["host"], sap_config["port"],
                     bw_status=403) as proxy:
        yield CliRunner(
            binary_path=cli.binary,
            host="127.0.0.1",
            port=proxy.port,
            user=sap_config["user"],
            password=sap_config["password"],
            client=sap_config["client"],
        )


class TestUnactivatedService:
    """A 403 from a service that is not activated is not a CSRF failure."""

    def test_not_reported_as_a_csrf_problem(self, cli_bw_off):
        result = cli_bw_off.run_fail("bw", "discover")
        assert "csrf_token" not in result.stderr
        assert "CSRF token may be invalid" not in result.stderr

    def test_classified_as_authorization(self, cli_bw_off):
        result = cli_bw_off.run_fail("bw", "discover")
        assert '"category":"authorization"' in result.stderr.replace(" ", "")
        assert result.returncode == 1

    def test_carries_an_actionable_hint(self, cli_bw_off):
        """The error should say where to switch the service on."""
        result = cli_bw_off.run_fail("bw", "discover")
        assert "SICF" in result.stderr
        assert "/sap/bw/modeling/" in result.stderr


class TestActivateWithHandleRejected:
    """`--activate` cannot be combined with `--handle`."""

    def test_rejected_before_anything_is_written(self, cli, tmp_path):
        src = tmp_path / "src.abap"
        src.write_text("* nothing\n")
        result = cli.run_fail(
            "source", "write",
            "/sap/bc/adt/oo/classes/zcl_does_not_matter/source/main",
            "--file", str(src),
            "--handle", "DEADBEEF",
            "--activate",
        )
        assert result.returncode == 99
        # Names both flags and the way out.
        assert "--activate" in result.stderr
        assert "--handle" in result.stderr
        assert "object unlock" in result.stderr


class TestInvalidInputIsValidation:
    """A wrong argument is not an internal error: same exit code 99 (#84,
    not breaking), but category "validation" in the JSON, so a script or an
    agent can tell "fix your call" from "erpl-adt is broken"."""

    @pytest.mark.parametrize("args", [
        ("transport", "show", "NOT-A-NUMBER"),             # ADT layer: malformed number
        ("source", "check", "/sap/bc/adt/oo/classes/cl_abap_random/source/main",
         "--version", "latest"),                            # ADT layer: unknown version
        ("object", "delete", "/sap/bc/adt/oo/classes/zzz", "--transprt", "X"),  # strict flag
        ("transport", "show"),                              # CLI: missing argument
    ], ids=["malformed-transport", "unknown-version", "misspelled-flag", "missing-arg"])
    def test_exit_99_category_validation(self, cli, args):
        result = cli.run(*args)
        assert result.returncode == 99, result.stdout + result.stderr
        error = json.loads(result.stderr)["error"]
        assert error["category"] == "validation", error
