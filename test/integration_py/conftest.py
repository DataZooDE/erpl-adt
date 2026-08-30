"""Shared fixtures for ADT integration tests (CLI-based)."""

import os
import random
import socket
import sys
import time

import pytest

from adt_proxy import Sap740Proxy
from bw_activation import ensure_bw_activated
from cli_runner import CliRunner


# ---------------------------------------------------------------------------
# Session start: make sure BW is up before anything runs
# ---------------------------------------------------------------------------

def pytest_sessionstart(session):
    """Probe the BW Modeling API and activate it if it is down.

    Runs here rather than in a fixture because switching BW on restarts the SAP
    instance, which takes minutes — far past the per-test `--timeout`, which
    also covers fixture setup. `pytest_sessionstart` is outside that clock.

    Best-effort by design: on any system that cannot or must not be modified
    this is a single HTTP probe and the BW suites skip exactly as before. See
    bw_activation.py for the guard rails.
    """
    password = os.getenv("SAP_PASSWORD")
    if not password:
        return  # No SAP system configured — every test skips anyway.

    # Don't restart a system for BW tests that were deselected.
    markexpr = session.config.getoption("-m", default="") or ""
    if "not bw" in markexpr:
        return

    ensure_bw_activated(
        host=os.getenv("SAP_HOST", "localhost"),
        port=int(os.getenv("SAP_PORT", "50000")),
        user=os.getenv("SAP_USER", "DEVELOPER"),
        password=password,
        client=os.getenv("SAP_CLIENT", "001"),
        log=lambda message: print(f"[bw] {message}", file=sys.stderr, flush=True),
    )


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
# SAP_BASIS 7.40 emulation (GitHub issue #35)
# ---------------------------------------------------------------------------

@pytest.fixture(scope="session")
def cli_740(sap_config, cli):
    """A CliRunner talking to the live SAP system through the 7.40 proxy.

    The proxy forwards everything to the real system except
    `GET /sap/bc/adt/packages/<name>`, which it answers with 404 — exactly what
    a 7.40 ICF tree does, because the per-package object resource does not
    exist there. All other responses are real SAP data.
    """
    with Sap740Proxy(sap_config["host"], sap_config["port"]) as proxy:
        yield CliRunner(
            binary_path=cli.binary,
            host="127.0.0.1",
            port=proxy.port,
            user=sap_config["user"],
            password=sap_config["password"],
            client=sap_config["client"],
        )


@pytest.fixture(scope="session")
def cli_740_no_collection(sap_config, cli):
    """Like `cli_740`, but also hides the bare `/sap/bc/adt/packages` collection.

    Emulates the 7.40 discovery document, where the Packages workspace lists
    only the `settings` service.
    """
    with Sap740Proxy(sap_config["host"], sap_config["port"],
                     strip_packages_collection=True) as proxy:
        yield CliRunner(
            binary_path=cli.binary,
            host="127.0.0.1",
            port=proxy.port,
            user=sap_config["user"],
            password=sap_config["password"],
            client=sap_config["client"],
        )


@pytest.fixture(scope="session")
def empty_package(cli):
    """Create an existing-but-empty local package; delete it on teardown.

    "Empty" and "non-existent" are different states, and SAP's nodestructure
    endpoint answers HTTP 200 with an empty body for both — which is why
    package existence needs a separate oracle.
    """
    name = f"$ZEMPTY_{random.randint(10000, 99999)}"
    cli.run_ok(
        "object", "create",
        "--type", "DEVC/K",
        "--name", name,
        "--package", "$TMP",
        "--description", "Integration test empty package",
        "--responsible", cli.user,
    )
    yield name
    cli.run("object", "delete", f"/sap/bc/adt/packages/{name.lower()}")


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
    """Probe BW discovery endpoint once. Skip all BW tests if unavailable.

    `pytest_sessionstart` has already tried to switch BW on by this point, so
    reaching the skip means BW is genuinely out of reach — a remote system, no
    Docker, or SAP_BW_AUTOACTIVATE=never.
    """
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


def find_active_object(cli, tlogo, probe=25):
    """Return the name of an *active* object of `tlogo`, or None.

    `bw read` addresses the active version by default, so an object that only
    exists inactive (someone's work in progress, a half-finished fixture) is
    not a usable read subject: SAP answers "Version 'A' ... does not exist".
    Search results carry the status, so pick on it rather than taking whatever
    happens to sort first.
    """
    result = cli.run("bw", "search", "*", "--max", str(probe), "--type", tlogo)
    if result.returncode != 0:
        return None
    stdout = result.stdout.strip()
    if not stdout:
        return None
    import json as _json
    for row in _json.loads(stdout):
        if row.get("status") == "active":
            return row["name"]
    return None
