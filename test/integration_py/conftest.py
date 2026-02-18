"""Shared fixtures for ADT integration tests (CLI-based)."""

import os
import random
import socket
import time

import pytest

from cli_runner import CliRunner


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def sap_config():
    """Load SAP connection config from environment variables."""
    password = os.getenv("SAP_PASSWORD")
    if not password:
        pytest.skip("SAP_PASSWORD not set — skipping integration tests")
    return {
        "host": os.getenv("SAP_HOST", "localhost"),
        "port": int(os.getenv("SAP_PORT", "50000")),
        "user": os.getenv("SAP_USER", "DEVELOPER"),
        "password": password,
        "client": os.getenv("SAP_CLIENT", "001"),
    }


# ---------------------------------------------------------------------------
# CLI runner (session-scoped)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def cli(sap_config):
    """Create a CliRunner for the entire test session.

    Waits for the SAP system to be reachable via TCP.
    """
    # Resolve binary path: prefer ERPL_ADT_BINARY env var, fall back to build dir.
    binary = os.getenv(
        "ERPL_ADT_BINARY",
        os.path.join(os.path.dirname(__file__), "..", "..", "build", "erpl-adt"),
    )
    assert os.path.isfile(binary), f"Binary not found: {binary}"

    runner = CliRunner(
        binary_path=binary,
        host=sap_config["host"],
        port=sap_config["port"],
        user=sap_config["user"],
        password=sap_config["password"],
        client=sap_config["client"],
    )

    # Wait for SAP system to be reachable (up to 5 minutes).
    deadline = time.time() + 300
    while time.time() < deadline:
        try:
            with socket.create_connection(
                (sap_config["host"], sap_config["port"]), timeout=5
            ):
                break
        except OSError:
            time.sleep(10)
    else:
        pytest.fail("SAP system not reachable after 5 minutes")

    return runner


# ---------------------------------------------------------------------------
# Session file fixture (per-test temp file for stateful operations)
# ---------------------------------------------------------------------------

@pytest.fixture
def session_file(tmp_path):
    """Return a temporary session file path for stateful CLI operations."""
    return str(tmp_path / "session.json")


# ---------------------------------------------------------------------------
# Test class fixture
# ---------------------------------------------------------------------------

@pytest.fixture
def test_class_name():
    """Generate a unique test class name."""
    suffix = random.randint(10000, 99999)
    return f"ZTEST_INTEG_{suffix}"


@pytest.fixture
def test_class(cli, test_class_name):
    """Create a test ABAP class in $TMP, yield info dict, delete on teardown."""
    name = test_class_name

    data = cli.run_ok(
        "object", "create",
        "--type", "CLAS/OC",
        "--name", name,
        "--package", "$TMP",
        "--description", "Integration test class",
    )
    uri = data.get("uri", f"/sap/bc/adt/oo/classes/{name.lower()}")

    yield {"name": name, "uri": uri}

    # Teardown: auto-lock mode handles lock→delete→unlock atomically.
    cli.run("object", "delete", uri)


# ---------------------------------------------------------------------------
# E2E test context (class-scoped)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def e2e_context(cli, tmp_path_factory):
    """Class-scoped context for E2E tests.

    Provides a shared dict for threading data between ordered test steps.
    Best-effort cleanup deletes the ABAP class on teardown.
    """
    tmp = tmp_path_factory.mktemp("e2e")
    name = f"ZTEST_E2E_{random.randint(10000, 99999)}"
    ctx = {
        "cli": cli,
        "tmp": tmp,
        "name": name,
        "session_file": str(tmp / "session.json"),
        "uri": None,
        "source_uri": None,
    }
    yield ctx
    # Best-effort cleanup: delete the class if it was created.
    uri = ctx.get("uri")
    if uri:
        # Auto-lock mode handles lock→delete→unlock atomically.
        cli.run("object", "delete", uri)


# ---------------------------------------------------------------------------
# Session-scoped BW availability fixtures (shared across bw test files)
# ---------------------------------------------------------------------------

import json as _json  # noqa: E402 — local alias to avoid polluting namespace


@pytest.fixture(scope="session")
def bw_available(cli):
    """Probe BW discovery endpoint once. Skip all BW tests if unavailable."""
    result = cli.run("bw", "discover")
    if result.returncode != 0:
        pytest.skip("BW Modeling API not available on this system")
    data = _json.loads(result.stdout.strip()) if result.stdout.strip() else []
    if not data:
        pytest.skip("BW discovery returned no services")
    return data


@pytest.fixture(scope="session")
def bw_has_search(cli, bw_available):
    """Check if BW search service is available and activated."""
    terms = {s.get("term", "") for s in bw_available}
    if "bwSearch" not in terms and "search" not in terms:
        pytest.skip("BW search service not available")
    # Probe the search endpoint — discovery may list it even if not activated
    result = cli.run("bw", "search", "*", "--max", "1")
    if result.returncode != 0:
        stderr = result.stderr.strip().lower()
        if "not activated" in stderr or "not implemented" in stderr:
            pytest.skip("BW search service listed but not activated")
        pytest.fail(f"BW search probe failed unexpectedly: {result.stderr.strip()}")
    return True


@pytest.fixture(scope="session")
def bw_has_adso(bw_available):
    """Check if ADSO service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "adso" not in terms:
        pytest.skip("BW ADSO service not available")
    return True


@pytest.fixture(scope="session")
def bw_has_cto(bw_available):
    """Check if BW transport organizer (CTO) service is available."""
    terms = {s.get("term", "") for s in bw_available}
    if "cto" not in terms:
        pytest.skip("BW CTO (transport) service not available")
    return True


@pytest.fixture(scope="session")
def bw_terms(bw_available):
    return {s.get("term", "") for s in bw_available}
