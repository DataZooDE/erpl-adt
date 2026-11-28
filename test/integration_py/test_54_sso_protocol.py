"""Actual CLI/MCP processes using client certificates (not a SAP E2E substitute)."""
import json
import os
from pathlib import Path
import ssl
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import pytest

@pytest.fixture
def mtls_peer(tmp_path):
    cert, key = tmp_path / "identity.pem", tmp_path / "identity.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
        "-keyout", str(key), "-out", str(cert)], check=True, capture_output=True)
    received = []
    identity = {"userName": "CERTUSER", "client": "040"}
    discovery = (Path(__file__).parent.parent / "testdata/discovery_response.xml").read_bytes()
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            received.append((self.path, dict(self.headers), self.connection.getpeercert()))
            body = json.dumps(identity).encode() if self.path == "/sap/bc/adt/core/http/systeminformation" else discovery
            status = identity.get("http_status", 200)
            if self.path == "/sap/bc/adt/core/http/systeminformation" and self.headers.get("Accept") != "application/vnd.sap.adt.core.http.systeminformation.v1+json":
                status = 406
                body = b"<error>ExceptionResourceNotAcceptable</error>"
            self.send_response(status if self.path == "/sap/bc/adt/core/http/systeminformation" else 200)
            self.send_header("Content-Type", "application/xml")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *args):
            pass
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    context.load_verify_locations(cert)
    context.verify_mode = ssl.CERT_REQUIRED
    server.socket = context.wrap_socket(server.socket, server_side=True)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        yield server.server_port, cert, key, received, identity
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)

@pytest.mark.parametrize("entry", ["cli", "mcp", "saved-cli", "saved-mcp"])
def test_x509_entry_points_have_no_password_fallback(mtls_peer, tmp_path, entry):
    port, cert, key, received, _ = mtls_peer
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    env.update(SAP_PASSWORD="must-not-be-sent", ERPL_ADT_TELEMETRY="0")
    flags = ["--host", "localhost", "--port", str(port), "--client", "040", "--https",
             "--auth", "x509", "--client-certificate", str(cert), "--client-key", str(key),
             "--ca-certificate", str(cert)]
    if entry.startswith("saved-"):
        login = subprocess.run([binary, "login", *flags], cwd=tmp_path, env=env,
                               capture_output=True, text=True, timeout=15)
        assert login.returncode == 0, login.stderr
        assert not json.loads((tmp_path / ".adt.creds").read_text()).get("password")
        flags = []
    if entry.endswith("mcp"):
        requests = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                "protocolVersion": "2025-03-26", "capabilities": {},
                "clientInfo": {"name": "sso-contract", "version": "1"}}},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {
                "name": "adt_discover", "arguments": {}}}]
        result = subprocess.run([binary, "mcp", *flags], input="".join(json.dumps(r)+"\n" for r in requests),
                                cwd=tmp_path, env=env, text=True, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr
        response = next(json.loads(line) for line in result.stdout.splitlines() if json.loads(line).get("id") == 2)
        assert "error" not in response, response
        assert not response["result"].get("isError"), response
    else:
        result = subprocess.run([binary, "discover", "services", *flags, "--json"], cwd=tmp_path, env=env,
                                capture_output=True, text=True, timeout=20)
        assert result.returncode == 0, result.stderr
    assert received
    assert received[0][0] == "/sap/bc/adt/core/http/systeminformation"
    for _, headers, identity in received:
        assert identity, "TLS peer must authenticate a client certificate"
        assert all(name.lower() != "authorization" for name in headers), headers
        assert headers.get("sap-client") == "040"


def test_identity_http_error_is_not_accepted_as_a_user(mtls_peer, tmp_path):
    port, cert, key, _, identity = mtls_peer
    identity["http_status"] = 401
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    result = subprocess.run([binary, "discover", "services", "--host", "localhost", "--port", str(port),
        "--client", "040", "--https", "--auth", "x509", "--client-certificate", str(cert),
        "--client-key", str(key), "--ca-certificate", str(cert), "--json"],
        cwd=tmp_path, env=env, text=True, capture_output=True, timeout=20)
    assert result.returncode == 1, result.stderr
    assert json.loads(result.stderr)["error"]["category"] == "authentication"


def test_http_mode_rejects_snc_settings_before_connecting(tmp_path):
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    result = subprocess.run([binary, "discover", "services", "--host", "127.0.0.1", "--port", "1",
        "--auth", "basic", "--snc-library", "must-not-be-ignored", "--json", "--timeout", "1"],
        cwd=tmp_path, env=env, text=True, capture_output=True, timeout=10)
    assert result.returncode == 99, result.stderr
    assert "SNC settings require" in json.loads(result.stderr)["error"]["message"]


def test_basic_yaml_password_environment_still_works(tmp_path):
    import base64
    from test_12_protocol_contracts import StubAdtServer
    expected = "Basic " + base64.b64encode(b"TESTUSER:fixture-password").decode()
    discovery = (Path(__file__).parent.parent / "testdata/discovery_response.xml").read_text()
    def get(request):
        if request.headers.get("Authorization") != expected:
            return 401, {}, "Authentication required"
        return 200, {"Content-Type": "application/xml"}, discovery
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    env["FIXTURE_ONLY_PASSWORD"] = "fixture-password"
    with StubAdtServer({("GET", "*"): get}) as server:
        profile = tmp_path / "connection.yaml"
        profile.write_text(json.dumps({"connection": {"host": "127.0.0.1", "port": server.port,
            "client": "001", "user": "TESTUSER", "password_env": "FIXTURE_ONLY_PASSWORD"}}))
        result = subprocess.run([binary, "discover", "services", "--config", str(profile), "--json"],
            cwd=tmp_path, env=env, text=True, capture_output=True, timeout=20)
    assert result.returncode == 0, result.stderr


def test_snc_rejects_https_options_before_connecting(tmp_path):
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    result = subprocess.run([binary, "discover", "services", "--host", "127.0.0.1",
        "--auth", "snc", "--snc-library", "unused", "--snc-partner", "p:CN=unused",
        "--https", "--json"], cwd=tmp_path, env=env, text=True, capture_output=True, timeout=10)
    assert result.returncode == 99, result.stderr
    assert "HTTPS settings cannot" in json.loads(result.stderr)["error"]["message"]


@pytest.mark.skipif(os.name == "nt", reason="POSIX terminal regression")
def test_explicit_sso_login_does_not_enter_password_wizard(tmp_path):
    import pty
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    master, slave = pty.openpty()
    try:
        process = subprocess.Popen([binary, "login", "--auth", "snc", "--snc-library", "unused",
            "--snc-partner", "p:CN=unused"], cwd=tmp_path, env=env, stdin=slave,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            stdout, stderr = process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            pytest.fail("Explicit SNC login entered the interactive password wizard")
        assert "Password" not in stdout + stderr
    finally:
        os.close(master)
        os.close(slave)


def test_snc_session_file_is_rejected_before_connecting(tmp_path):
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    result = subprocess.run([binary, "discover", "services", "--auth", "snc", "--host", "127.0.0.1",
        "--snc-library", "unused", "--snc-partner", "p:CN=unused", "--session-file", str(tmp_path / "session.json"),
        "--json"], cwd=tmp_path, env=env, text=True, capture_output=True, timeout=10)
    assert result.returncode == 99, result.stderr
    assert "session-file" in json.loads(result.stderr)["error"]["message"]


@pytest.mark.parametrize("mode,extra", [("snc", ["--https"]), ("basic", [])])
def test_login_rejects_incompatible_transport_options(tmp_path, mode, extra):
    binary = str(Path(os.getenv("ERPL_ADT_BINARY", Path(__file__).parents[2] / "build/erpl-adt")).resolve())
    env = {k: v for k, v in os.environ.items() if not k.startswith(("SAP_", "ERPL_ADT_"))}
    result = subprocess.run([binary, "login", "--auth", mode, "--host", "localhost",
        "--user", "FIXTURE", "--password", "fixture", "--snc-library", "unused",
        "--snc-partner", "p:CN=unused", *extra], cwd=tmp_path, env=env,
        text=True, capture_output=True, timeout=10)
    assert result.returncode == 99, result.stdout + result.stderr
    assert not (tmp_path / ".adt.creds").exists()
