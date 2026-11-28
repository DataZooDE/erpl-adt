"""Live helper failure with an independent SM05 oracle.

Only the terminal HTTP response is injected. Helper creation, source writes,
activation, cookies and logoff use SAP. SECURITY_CONTEXT is the table read by
SHTTPSESSIONS_MONITORING (SM05). Held sessions belong to this test's second
user; cleanup logs off only cookies created here.
"""

import base64
import http.client
import threading
import xml.etree.ElementTree as ET
from pathlib import Path
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from adt_proxy import _HOP_BY_HOP
from cli_runner import CliRunner

pytestmark = pytest.mark.session_logon


@pytest.mark.parametrize("command", ["deploy", "export"])
@pytest.mark.parametrize("status", [400, 429])
def test_live_helper_refusal_preserves_security_sessions(colleague_cli, hana, tmp_path, status, command):
    cli = colleague_cli
    # The oracle refuses remote systems before any sessions are created.
    user = cli.user.replace("'", "''")
    client = cli.client.replace("'", "''")

    def sessions():
        return {tuple(row) for row in hana.query(
            f"SELECT ID FROM {hana.table('SECURITY_CONTEXT')} "
            f"WHERE USERID = '{user}' AND CLIENT = '{client}' AND HAS_SUCCESSOR = 0")}

    original = sessions()
    # This disposable A4H trial has a per-user limit of 100. Reserve two
    # slots for the workflow, accounting for sessions already present.
    if len(original) >= 98:
        pytest.skip("test user already has 98 sessions; cannot reserve workflow slots")
    authorization = "Basic " + base64.b64encode(
        f"{cli.user}:{cli.password}".encode()).decode()
    held = []
    helper_uris = set()
    after_failure = []
    injected = []

    def request(path, cookie=""):
        conn = http.client.HTTPConnection(cli.host, cli.port, timeout=60)
        try:
            headers = {"Authorization": authorization, "sap-client": cli.client, "Accept": "*/*"}
            if cookie:
                headers["Cookie"] = cookie
            conn.request("GET", path, headers=headers)
            response = conn.getresponse()
            response.read()
            return response.status, response.getheaders()
        finally:
            conn.close()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def handle_request(self):
            payload = self.rfile.read(int(self.headers.get("Content-Length", 0)))
            if injected and not self.path.startswith("/sap/public/bc/icf/logoff"):
                after_failure.append((self.command, self.path))
            if "/classrun/" in self.path and not injected:
                injected.append(self.path)
                body = ("<html><title>Service cannot be reached</title>"
                        "<body>400 Session not found</body></html>" if status == 400
                        else "<html><title>429 Too Many Logons</title></html>").encode()
                response_status, headers = status, []
            else:
                conn = http.client.HTTPConnection(cli.host, cli.port, timeout=120)
                try:
                    forwarded = {k: v for k, v in self.headers.items()
                                 if k.lower() not in _HOP_BY_HOP}
                    forwarded["Host"] = f"{cli.host}:{cli.port}"
                    conn.request(self.command, self.path, body=payload, headers=forwarded)
                    response = conn.getresponse()
                    body = response.read()
                    response_status, headers = response.status, response.getheaders()
                    if (self.command == "POST" and
                            self.path.split("?", 1)[0] == "/sap/bc/adt/oo/classes" and
                            response_status in (200, 201)):
                        name = ET.fromstring(payload).get("{http://www.sap.com/adt/core}name")
                        assert name and name.upper().startswith("ZCL_ERPLHLP_")
                        helper_uris.add("/sap/bc/adt/oo/classes/" + name.lower())
                finally:
                    conn.close()
            self.send_response(response_status)
            for key, value in headers:
                if key.lower() not in _HOP_BY_HOP | {"content-length"}:
                    self.send_header(key, value)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        do_GET = do_POST = do_PUT = do_DELETE = handle_request

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        for _ in range(98 - len(original)):
            response_status, headers = request("/sap/bc/adt/discovery")
            cookies = SimpleCookie()
            for key, value in headers:
                if key.lower() == "set-cookie":
                    cookies.load(value)
            cookie = "; ".join(f"{k}={v.value}" for k, v in cookies.items())
            held.append(cookie)
            assert response_status == 200 and cookie
        before = sessions()
        assert len(before) == 98, "SAP did not retain exactly 98 sessions"
        proxied = CliRunner(cli.binary, "127.0.0.1", server.server_port,
                            cli.user, cli.password, cli.client)
        if command == "deploy":
            source = Path(__file__).resolve().parents[1] / "testdata/abapgit/PROG/src/zag_unit_test.prog.xml"
            args = [str(source)]
        else:
            args = [str(tmp_path / "export"), "--object", "PROG:ZAG_UNIT_TEST"]
        result = proxied.run("abapgit", command, *args, "--package", "$TMP")
        assert injected, result.stdout + result.stderr
        assert helper_uris, "workflow did not create a real SAP helper"
        assert result.returncode != 0
        assert "SM05" in result.stdout + result.stderr
        assert not after_failure, after_failure
        assert sessions() == before, "failed workflow leaked or evicted SM05 sessions"
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
        for cookie in held:
            request("/sap/public/bc/icf/logoff", cookie)
        # Cleanup is an explicit fresh command after measuring the failed workflow.
        for uri in helper_uris:
            result = cli.run("object", "delete", uri)
            assert result.returncode == 0, result.stdout + result.stderr
    assert sessions() == original
