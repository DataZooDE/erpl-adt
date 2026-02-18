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
