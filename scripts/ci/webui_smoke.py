#!/usr/bin/env python3
"""CI smoke check: does the embedded catalog Explorer web UI actually start?

Starts `<binary> catalog webui <scratch>.duckdb --port <n>`, polls /healthz,
then asserts the embedded Flutter app is served (not the 501 "not embedded"
fallback that a build without `make webui` first would silently return).

No SAP system involved — DuckDbCatalogStore::Open creates a fresh schema for
a nonexistent path, so a brand-new scratch file is enough. Deliberately not a
pytest file under test/integration_py/: every fixture there requires or pulls
in SAP_PASSWORD, and this is a CI-infra check, not an ADT integration test.

Usage:
    python3 scripts/ci/webui_smoke.py --binary build/erpl-adt
"""

import argparse
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

HEALTHZ_TIMEOUT_SECONDS = 15
HEALTHZ_POLL_INTERVAL_SECONDS = 0.5
PROCESS_TERMINATE_TIMEOUT_SECONDS = 5


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def get(url: str):
    """GET url, returning (status, headers, body) even on a non-2xx response."""
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            return resp.status, dict(resp.headers), resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers or {}), e.read().decode("utf-8", "replace")


def wait_for_healthz(base_url: str, proc: subprocess.Popen) -> None:
    deadline = time.monotonic() + HEALTHZ_TIMEOUT_SECONDS
    last_error = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise AssertionError(
                f"catalog webui exited early (code {proc.returncode}) before /healthz responded"
            )
        try:
            status, _, _ = get(f"{base_url}/healthz")
            if status == 200:
                return
            last_error = f"got HTTP {status}"
        except urllib.error.URLError as e:
            last_error = str(e)
        time.sleep(HEALTHZ_POLL_INTERVAL_SECONDS)
    raise AssertionError(f"/healthz never returned 200 within {HEALTHZ_TIMEOUT_SECONDS}s: {last_error}")


def run_checks(base_url: str) -> None:
    # The critical regression check: a 501 here means ERPL_ADT_HAVE_WEBUI
    # wasn't actually defined for this binary — i.e. the Flutter build
    # silently didn't take effect before CMake configured.
    status, headers, body = get(f"{base_url}/")
    assert status == 200, f"GET / returned {status} (expected 200 — is the web UI embedded?): {body[:500]}"
    content_type = headers.get("Content-Type", "")
    assert content_type.startswith("text/html"), f"GET / Content-Type was '{content_type}', expected text/html"
    assert "<html" in body, "GET / body doesn't look like HTML (missing '<html')"
    index_body = body

    status, headers, _ = get(f"{base_url}/main.dart.js")
    assert status == 200, f"GET /main.dart.js returned {status} (expected 200)"
    content_type = headers.get("Content-Type", "")
    assert content_type == "application/javascript", (
        f"GET /main.dart.js Content-Type was '{content_type}', expected application/javascript"
    )

    # SPA fallback: go_router does client-side routing, so a deep link with
    # no matching embedded file must still resolve to index.html (200), not
    # a raw 404 — mirrors test_mcp_http_server.cpp's own assertion.
    status, _, deep_link_body = get(f"{base_url}/entity/does-not-exist")
    assert status == 200, f"GET /entity/does-not-exist returned {status} (expected 200, SPA fallback)"
    assert deep_link_body == index_body, "SPA fallback body doesn't match index.html"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, help="Path to the erpl-adt binary")
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    binary = Path(args.binary).resolve()
    if not binary.exists():
        print(f"FAIL: binary not found: {binary}", file=sys.stderr)
        return 1

    port = free_port()
    base_url = f"http://{args.host}:{port}"

    with tempfile.TemporaryDirectory(prefix="erpl-adt-webui-smoke-") as scratch_dir:
        db_path = Path(scratch_dir) / "smoke.duckdb"
        proc = subprocess.Popen(
            [str(binary), "catalog", "webui", str(db_path), "--port", str(port), "--host", args.host],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            wait_for_healthz(base_url, proc)
            run_checks(base_url)
        except AssertionError as e:
            print(f"FAIL: {e}", file=sys.stderr)
            if proc.poll() is not None and proc.stdout is not None:
                print("--- process output ---", file=sys.stderr)
                print(proc.stdout.read(), file=sys.stderr)
            return 1
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=PROCESS_TERMINATE_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=PROCESS_TERMINATE_TIMEOUT_SECONDS)

    print("PASS: embedded catalog web UI serves index.html, main.dart.js, and the SPA fallback")
    return 0


if __name__ == "__main__":
    sys.exit(main())
