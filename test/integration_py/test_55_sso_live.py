"""Opt-in real SAP SSO E2E through the actual CLI/MCP binary.

Set SAP_SNC_TEST_CONFIG or SAP_X509_TEST_CONFIG to a YAML connection profile.
The target must permit disposable $TMP classes. No Basic-auth fixture is used.
SAP_SSO_SESSION_ORACLE=1 additionally checks local Docker SM05 sessions; set
SAP_SSO_EXPECTED_USER and run against that same local trial without concurrent
HTTP tests for this user. It never terminates sessions belonging to others.
"""
import json
import os
import queue
import subprocess
import threading
import uuid
from pathlib import Path

import pytest


class McpProcess:
    def __init__(self, binary, config):
        env = os.environ.copy()
        for key in ("SAP_PASSWORD", "SAP_USER", "SAP_PASSWD", "ERPL_ADT_PASSWORD", "ERPL_ADT_USER"):
            env.pop(key, None)
        self.process = subprocess.Popen(
            [binary, "--config", str(config), "mcp"], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
        self.messages = queue.Queue()
        self.errors = []
        self.next_id = 0

        def read_stdout():
            for line in self.process.stdout:
                if line.lstrip().startswith("{"):
                    self.messages.put(json.loads(line))
            self.messages.put(None)

        def read_stderr():
            self.errors.extend(self.process.stderr)

        self.reader = threading.Thread(target=read_stdout, daemon=True)
        self.error_reader = threading.Thread(target=read_stderr, daemon=True)
        self.reader.start()
        self.error_reader.start()

    def call(self, name, arguments, *, expect_error=False):
        self.next_id += 1
        message = {"jsonrpc": "2.0", "id": self.next_id, "method": "tools/call",
                   "params": {"name": name, "arguments": arguments}}
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()
        response = self.messages.get(timeout=180)
        assert response is not None, "MCP exited: " + "".join(self.errors)
        assert response.get("id") == self.next_id, response
        assert "error" not in response, response
        result = response["result"]
        assert bool(result.get("isError")) == expect_error, result
        return "\n".join(item.get("text", "") for item in result.get("content", []))

    def close(self):
        if not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=10)
        self.reader.join(timeout=5)
        self.error_reader.join(timeout=5)


@pytest.mark.parametrize("profile_env", ["SAP_SNC_TEST_CONFIG", "SAP_X509_TEST_CONFIG"])
def test_sso_mcp_unicode_write_activate_failure_cleanup(profile_env):
    profile = os.getenv(profile_env)
    if not profile:
        pytest.skip(f"Set {profile_env} to opt into the real SAP SSO E2E")
    config = Path(profile)
    assert config.is_file(), config
    binary = os.getenv("ERPL_ADT_BINARY", str(Path(__file__).resolve().parents[2] / "build" / "erpl-adt"))
    name = "ZCL_ERPL_SSO_" + uuid.uuid4().hex[:12].upper()
    uri = "/sap/bc/adt/oo/classes/" + name.lower()
    source_uri = uri + "/source/main"
    marker = "Grüsse 日本語 😀"
    source = f"""CLASS {name.lower()} DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    CLASS-METHODS example.
ENDCLASS.
CLASS {name.lower()} IMPLEMENTATION.
  METHOD example.
* {marker}
  ENDMETHOD.
ENDCLASS.
"""
    session_ids = None
    original_sessions = set()
    if os.getenv("SAP_SSO_SESSION_ORACLE") == "1":
        from hana_oracle import oracle
        user = os.environ["SAP_SSO_EXPECTED_USER"].replace("'", "''")
        client = os.getenv("SAP_CLIENT", "001").replace("'", "''")
        hana = oracle()

        def session_ids():
            return {tuple(row) for row in hana.query(
                f"SELECT ID FROM {hana.table('SECURITY_CONTEXT')} "
                f"WHERE USERID = '{user}' AND CLIENT = '{client}' AND HAS_SUCCESSOR = 0")}

        original_sessions = session_ids()
        print(f"SM05 baseline sessions: {len(original_sessions)}")
    mcp = McpProcess(binary, config)
    created = False
    try:
        mcp.call("adt_discover", {})
        if session_ids and profile_env == "SAP_X509_TEST_CONFIG":
            observed = session_ids() - original_sessions
            print(f"SM05 observed new HTTP sessions: {len(observed)}")
            assert observed, "SM05 oracle did not observe the HTTP session"
        mcp.call("bw_discover", {})
        mcp.call("adt_search", {"query": "CL_ABAP_RANDOM", "max_results": 1})
        mcp.call("adt_create_object", {"object_type": "CLAS/OC", "name": name,
                                      "package_name": "$TMP", "description": "SSO E2E disposable class"})
        created = True
        mcp.call("adt_write_source", {"uri": source_uri, "source": source})
        inactive = mcp.call("adt_read_source", {"uri": source_uri, "version": "inactive"})
        assert marker in inactive, inactive
        mcp.call("adt_activate", {"uri": uri, "type": "CLAS/OC", "name": name})
        active = mcp.call("adt_read_source", {"uri": source_uri, "version": "active"})
        assert marker in active, active
        metadata = json.loads(mcp.call("adt_read_object", {"uri": uri}))
        assert metadata["responsible"], metadata
        expected_user = os.getenv("SAP_SSO_EXPECTED_USER")
        if expected_user:
            assert metadata["responsible"] == expected_user, metadata
        # A rejected write must release its real lock/context before retry.
        for attempt in range(3):
            mcp.call("adt_write_source", {"uri": source_uri, "source": "not an ABAP class"},
                     expect_error=True)
            updated = source.replace(marker, marker + f" retry {attempt}")
            mcp.call("adt_write_source", {"uri": source_uri, "source": updated})
            mcp.call("adt_activate", {"uri": uri, "type": "CLAS/OC", "name": name})
            active = mcp.call("adt_read_source", {"uri": source_uri, "version": "active"})
            assert marker + f" retry {attempt}" in active, active
        # Deliberately leave an explicit lock held when stdin closes. A fresh
        # process must be able to acquire that same object's lock, proving the
        # underlying RFC/HTTP context was released on destruction.
        mcp.call("adt_lock", {"uri": uri})
        mcp.close()
        mcp = McpProcess(binary, config)
        mcp.call("adt_write_source", {"uri": source_uri, "source": source})
    finally:
        try:
            if created:
                mcp.call("adt_delete_object", {"uri": uri})
        finally:
            mcp.close()
    verify = McpProcess(binary, config)
    try:
        verify.call("adt_read_object", {"uri": uri}, expect_error=True)
    finally:
        verify.close()
    if session_ids:
        remaining = session_ids()
        print(f"SM05 final sessions: {len(remaining)}; new sessions remaining: {len(remaining - original_sessions)}")
        assert remaining <= original_sessions, "SSO workflow leaked new SM05 sessions"
