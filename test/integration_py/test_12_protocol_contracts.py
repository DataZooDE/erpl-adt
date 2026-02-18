"""Protocol-contract integration tests using a local ADT stub server."""

import json
import os
import socketserver
import threading
import time
from http.server import BaseHTTPRequestHandler

from cli_runner import CliRunner


def _binary_path():
    return os.getenv(
        "ERPL_ADT_BINARY",
        os.path.join(os.path.dirname(__file__), "..", "..", "build", "erpl-adt"),
    )


class _ThreadingTcpServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


class StubAdtServer:
    def __init__(self, callbacks):
        self._callbacks = callbacks
        self._server = None
        self._thread = None
        self.port = None

    def __enter__(self):
        callbacks = self._callbacks

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                self._dispatch("GET")

            def do_POST(self):
                self._dispatch("POST")

            def log_message(self, fmt, *args):
                return

            def _dispatch(self, method):
                callback = callbacks.get((method, self.path))
                if callback is None:
                    callback = callbacks.get((method, "*"))
                if callback is None:
                    self.send_response(404)
                    self.end_headers()
                    self.wfile.write(b"not found")
                    return

                status, headers, body = callback(self)
                self.send_response(status)
                for key, value in headers.items():
                    self.send_header(key, value)
                self.end_headers()
                self.wfile.write(body.encode("utf-8"))

        self._server = _ThreadingTcpServer(("127.0.0.1", 0), Handler)
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        if self._server is not None:
            self._server.shutdown()
            self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=2)


def _make_runner(port):
    return CliRunner(
        binary_path=_binary_path(),
        host="127.0.0.1",
        port=port,
        user="DEVELOPER",
        password="secret",
        client="001",
    )


def test_contract_missing_location_header_for_async_activate():
    def csrf(_):
        return 200, {"x-csrf-token": "stub-token"}, "<discovery/>"

    def activate(_):
        return 202, {}, "<accepted/>"

    def post_dispatch(req):
        if req.path.startswith("/sap/bc/adt/activation?method=activate"):
            return activate(req)
        return 404, {}, "not found"

    callbacks = {
        ("GET", "/sap/bc/adt/discovery"): csrf,
        ("POST", "*"): post_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("activate", "/sap/bc/adt/oo/classes/zcl_demo")

    assert result.returncode == 99
    err = json.loads(result.stderr)["error"]
    assert "Location" in err["message"]
    assert err["exit_code"] == 99


def test_contract_parse_failure_includes_line_diagnostics():
    def search(_):
        return 200, {"Content-Type": "application/xml"}, "<root>\n  <broken>\n</root>"

    def get_dispatch(req):
        if req.path.startswith(
            "/sap/bc/adt/repository/informationsystem/search?operation=quickSearch&query="
        ):
            return search(req)
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("search", "query", "Z*")

    assert result.returncode == 99
    err = json.loads(result.stderr)["error"]
    assert "parse" in err["message"].lower()


def test_contract_lock_conflict_maps_to_exit_6():
    def csrf(_):
        return 200, {"x-csrf-token": "stub-token"}, "<discovery/>"

    def lock(_):
        return 409, {}, "<error><message>locked</message></error>"

    def post_dispatch(req):
        if "_action=LOCK" in req.path:
            return lock(req)
        return 404, {}, "not found"

    callbacks = {
        ("GET", "/sap/bc/adt/discovery"): csrf,
        ("POST", "*"): post_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("object", "lock", "/sap/bc/adt/oo/classes/zcl_demo")

    assert result.returncode == 6
    err = json.loads(result.stderr)["error"]
    assert err["category"] == "lock_conflict"
    assert err["exit_code"] == 6


def test_contract_timeout_maps_to_exit_10():
    def slow_discovery(_):
        time.sleep(2)
        return 200, {"Content-Type": "application/xml"}, "<root/>"

    callbacks = {
        ("GET", "/sap/bc/adt/discovery"): slow_discovery,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("discover", "services", extra_flags=["--timeout", "1"], timeout=20)

    assert result.returncode == 10
    err = json.loads(result.stderr)["error"]
    assert err["category"] == "timeout"
    assert err["exit_code"] == 10


def test_contract_error_json_shape_is_stable():
    with StubAdtServer({}) as server:
        cli = _make_runner(server.port)
        result = cli.run("object", "lock", "not-a-uri")

    assert result.returncode == 99
    payload = json.loads(result.stderr)
    assert "error" in payload
    error = payload["error"]
    for key in ("category", "operation", "message", "exit_code"):
        assert key in error


def test_contract_bw_read_query_not_found_maps_to_exit_2():
    def get_dispatch(_):
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("bw", "read-query", "query", "ZQ_DOES_NOT_EXIST")

    assert result.returncode == 2
    err = json.loads(result.stderr)["error"]
    assert err["category"] == "not_found"
    assert err["exit_code"] == 2


def test_contract_bw_read_query_parse_failure_maps_to_exit_99():
    def get_dispatch(req):
        if req.path.startswith("/sap/bw/modeling/query/"):
            return 200, {"Content-Type": "application/xml"}, "<root>\n  <broken>\n</root>"
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("bw", "read-query", "query", "ZQ_PARSE_BROKEN")

    assert result.returncode == 99
    err = json.loads(result.stderr)["error"]
    assert "parse" in err["message"].lower()
    assert err["exit_code"] == 99


def test_contract_bw_read_query_endpoint_templates_lowercase_name():
    seen = []

    def get_dispatch(req):
        seen.append(req.path)
        if req.path.startswith("/sap/bw/modeling/query/zq_sales/a"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\" "
                "name=\"ZQ_SALES\" description=\"Sales\"/>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("bw", "read-query", "query", "ZQ_SALES")

    assert result.returncode == 0
    assert any("/sap/bw/modeling/query/zq_sales/a" in p for p in seen)


def test_contract_bw_read_query_415_retries_accept_fallbacks():
    seen_accepts = []

    def get_dispatch(req):
        if req.path.startswith("/sap/bw/modeling/query/zvar_fiscyear/a"):
            seen_accepts.append(req.headers.get("Accept", ""))
            if len(seen_accepts) < 3:
                return 415, {}, "unsupported media type"
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<query:variable xmlns:query=\"http://www.sap.com/bw/modeling/query\" "
                "name=\"ZVAR_FISCYEAR\" description=\"Fiscal Year\"/>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("bw", "read-query", "variable", "ZVAR_FISCYEAR")

    assert result.returncode == 0
    assert seen_accepts[0] == "application/vnd.sap.bw.modeling.variable-v1_10_0+xml"
    assert seen_accepts[1] == "application/vnd.sap.bw.modeling.variable-v1_9_0+xml"
    assert seen_accepts[2] == "application/xml"


def test_contract_bw_read_query_auto_upstream_planning_contract():
    def get_dispatch(req):
        if req.path.startswith("/sap/bw/modeling/query/zq_sales/a"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\" "
                "name=\"ZQ_SALES\" description=\"Sales\" infoProvider=\"ZCP_SALES\" "
                "infoProviderType=\"HCPR\"/>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        if req.path.startswith("/sap/bw/modeling/repo/is/bwsearch?"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                "xmlns:bwModel=\"http://www.sap.com/bw/modeling\">"
                "  <entry>"
                "    <title>DTP</title>"
                "    <id>/sap/bw/modeling/dtpa/DTP_ZSALES/a</id>"
                "    <content type=\"application/xml\">"
                "      <bwModel:searchResult objectName=\"DTP_ZSALES\" objectType=\"DTPA\" "
                "          objectVersion=\"A\" objectStatus=\"ACT\"/>"
                "    </content>"
                "  </entry>"
                "</feed>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        if req.path.startswith("/sap/bw/modeling/dtpa/dtp_zsales/a"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<dtpa name=\"DTP_ZSALES\">"
                "  <source objectName=\"ZSRC\" objectType=\"RSDS\" sourceSystem=\"LOCAL\"/>"
                "  <target objectName=\"ZCP_SALES\" objectType=\"HCPR\"/>"
                "</dtpa>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run("bw", "read-query", "query", "ZQ_SALES", "--upstream=auto")

    assert result.returncode == 0
    payload = json.loads(result.stdout)
    upstream = payload.get("upstream_resolution", {})
    assert upstream.get("mode") == "auto"
    assert upstream.get("selected_dtp") == "DTP_ZSALES"
    assert isinstance(upstream.get("complete"), bool)
    assert isinstance(upstream.get("steps"), int)
    assert isinstance(upstream.get("candidates"), list)


def test_contract_bw_read_query_auto_upstream_strict_ambiguous_fails():
    def get_dispatch(req):
        if req.path.startswith("/sap/bw/modeling/query/zq_sales/a"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\" "
                "name=\"ZQ_SALES\" description=\"Sales\" infoProvider=\"ZCP_SALES\" "
                "infoProviderType=\"HCPR\"/>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        if req.path.startswith("/sap/bw/modeling/repo/is/bwsearch?"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                "xmlns:bwModel=\"http://www.sap.com/bw/modeling\">"
                "  <entry><title>DTP A</title><id>/sap/bw/modeling/dtpa/DTP_A/a</id>"
                "    <content type=\"application/xml\">"
                "      <bwModel:searchResult objectName=\"DTP_A\" objectType=\"DTPA\" objectVersion=\"A\" objectStatus=\"ACT\"/>"
                "    </content>"
                "  </entry>"
                "  <entry><title>DTP B</title><id>/sap/bw/modeling/dtpa/DTP_B/a</id>"
                "    <content type=\"application/xml\">"
                "      <bwModel:searchResult objectName=\"DTP_B\" objectType=\"DTPA\" objectVersion=\"A\" objectStatus=\"ACT\"/>"
                "    </content>"
                "  </entry>"
                "</feed>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        if req.path.startswith("/sap/bw/modeling/dtpa/dtp_a/a"):
            return 200, {"Content-Type": "application/xml"}, (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<dtpa name=\"DTP_A\"><target objectName=\"ZCP_SALES\" objectType=\"HCPR\"/></dtpa>"
            )
        if req.path.startswith("/sap/bw/modeling/dtpa/dtp_b/a"):
            return 200, {"Content-Type": "application/xml"}, (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<dtpa name=\"DTP_B\"><target objectName=\"ZCP_SALES\" objectType=\"HCPR\"/></dtpa>"
            )
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run(
            "bw", "read-query", "query", "ZQ_SALES", "--upstream=auto", "--lineage-strict"
        )

    assert result.returncode == 99
    assert "strict upstream resolution failed" in result.stderr.lower()


def test_contract_bw_read_query_auto_upstream_ambiguous_composes_all_candidates():
    def get_dispatch(req):
        if req.path.startswith("/sap/bw/modeling/query/zq_sales/a"):
            body = (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<query:query xmlns:query=\"http://www.sap.com/bw/modeling/query\" "
                "name=\"ZQ_SALES\" description=\"Sales\" infoProvider=\"ZCP_SALES\" "
                "infoProviderType=\"HCPR\"/>"
            )
            return 200, {"Content-Type": "application/xml"}, body
        if req.path.startswith("/sap/bw/modeling/repo/is/bwsearch?"):
            if "objectType=TRFN" in req.path:
                return 200, {"Content-Type": "application/xml"}, (
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<feed xmlns=\"http://www.w3.org/2005/Atom\"/>"
                )
            return 200, {"Content-Type": "application/xml"}, (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                "xmlns:bwModel=\"http://www.sap.com/bw/modeling\">"
                "  <entry><title>DTP A</title><id>/sap/bw/modeling/dtpa/DTP_A/a</id>"
                "    <content type=\"application/xml\">"
                "      <bwModel:searchResult objectName=\"DTP_A\" objectType=\"DTPA\" objectVersion=\"A\" objectStatus=\"ACT\"/>"
                "    </content>"
                "  </entry>"
                "  <entry><title>DTP B</title><id>/sap/bw/modeling/dtpa/DTP_B/a</id>"
                "    <content type=\"application/xml\">"
                "      <bwModel:searchResult objectName=\"DTP_B\" objectType=\"DTPA\" objectVersion=\"A\" objectStatus=\"ACT\"/>"
                "    </content>"
                "  </entry>"
                "</feed>"
            )
        if req.path.startswith("/sap/bw/modeling/dtpa/dtp_a/a"):
            return 200, {"Content-Type": "application/xml"}, (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<dtpa name=\"DTP_A\">"
                "<source objectName=\"ZSRC\" objectType=\"RSDS\" sourceSystem=\"LOCAL\"/>"
                "<target objectName=\"ZCP_SALES\" objectType=\"HCPR\"/>"
                "</dtpa>"
            )
        if req.path.startswith("/sap/bw/modeling/dtpa/dtp_b/a"):
            return 200, {"Content-Type": "application/xml"}, (
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<dtpa name=\"DTP_B\">"
                "<source objectName=\"ZSRC\" objectType=\"RSDS\" sourceSystem=\"LOCAL\"/>"
                "<target objectName=\"ZCP_SALES\" objectType=\"HCPR\"/>"
                "</dtpa>"
            )
        return 404, {}, "not found"

    callbacks = {
        ("GET", "*"): get_dispatch,
    }

    with StubAdtServer(callbacks) as server:
        cli = _make_runner(server.port)
        result = cli.run(
            "bw", "read-query", "query", "ZQ_SALES",
            "--upstream=auto", "--upstream-no-xref", "--json-shape=truth"
        )

    assert result.returncode == 0
    payload = json.loads(result.stdout)
    resolution = payload.get("resolution", {})
    assert resolution.get("ambiguous") is True
    assert sorted(resolution.get("composed_candidates", [])) == ["DTP_A", "DTP_B"]
    assert isinstance(payload.get("provenance"), list)
    assert any(p.startswith("bw.lineage.compose") for p in payload["provenance"])

