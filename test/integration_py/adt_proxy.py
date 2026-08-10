"""Transparent ADT proxy that emulates one SAP_BASIS 7.40 routing difference.

Background (GitHub issue #35): on SAP_BASIS 7.40 the ADT discovery document
exposes only ``/sap/bc/adt/packages/settings`` — there is no per-package object
resource, so ``GET /sap/bc/adt/packages/<name>`` answers 404 for *every*
package, existing or not. On 7.5x / ABAP Cloud (the Docker trial we test
against) that resource exists and answers 200, so the 7.40 failure mode cannot
arise there.

This proxy sits in front of the real SAP system and reproduces exactly that one
difference: it returns 404 for ``GET /sap/bc/adt/packages/<name>`` and, when
``strip_packages_collection`` is set, drops the bare ``/sap/bc/adt/packages``
collection from the discovery document. Every other request — search,
nodestructure, CSRF, object CRUD — is forwarded to the live system untouched
and answered with real SAP data.

It is deliberately *not* a mock: no SAP response is synthesised except the 404
that a 7.40 ICF tree would produce by itself.
"""

import http.client
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Sub-resources that exist on 7.40 as well and must keep working. Everything
# else under /sap/bc/adt/packages/ is treated as a per-package object resource.
_PACKAGE_SUBRESOURCES = ("settings", "validation", "valuehelps")

_PACKAGE_OBJECT_RE = re.compile(r"^/sap/bc/adt/packages/([^/?]+)")

# Hop-by-hop headers that must not be relayed (RFC 7230 6.1).
_HOP_BY_HOP = {
    "connection", "keep-alive", "proxy-authenticate", "proxy-authorization",
    "te", "trailers", "transfer-encoding", "upgrade",
}


def _is_package_object_resource(path):
    """True if `path` addresses a per-package object resource."""
    match = _PACKAGE_OBJECT_RE.match(path)
    if not match:
        return False
    first_segment = match.group(1).lower()
    return not first_segment.startswith(_PACKAGE_SUBRESOURCES)


class _Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # Injected by Sap740Proxy.
    upstream_host = ""
    upstream_port = 0
    strip_packages_collection = False

    def log_message(self, format, *args):  # noqa: A002 — silence stderr spam
        pass

    def _handle(self):
        if self.command == "GET" and _is_package_object_resource(self.path):
            body = b"Not Found"
            self.send_response(404)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        length = int(self.headers.get("Content-Length") or 0)
        payload = self.rfile.read(length) if length else None

        headers = {
            k: v for k, v in self.headers.items()
            if k.lower() not in _HOP_BY_HOP and k.lower() != "accept-encoding"
        }
        # Ask upstream for identity encoding so response bodies stay
        # inspectable (the discovery rewrite below operates on raw bytes).
        headers["Accept-Encoding"] = "identity"
        headers["Host"] = f"{self.upstream_host}:{self.upstream_port}"

        conn = http.client.HTTPConnection(
            self.upstream_host, self.upstream_port, timeout=300,
        )
        try:
            conn.request(self.command, self.path, body=payload, headers=headers)
            response = conn.getresponse()
            body = response.read()

            if (self.strip_packages_collection
                    and self.path.startswith("/sap/bc/adt/discovery")):
                body = _strip_packages_collection(body)

            self.send_response(response.status)
            for key, value in response.getheaders():
                if key.lower() in _HOP_BY_HOP or key.lower() == "content-length":
                    continue
                self.send_header(key, value)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        finally:
            conn.close()

    do_GET = _handle
    do_POST = _handle
    do_PUT = _handle
    do_DELETE = _handle
    do_HEAD = _handle


def _strip_packages_collection(body):
    """Drop the bare `/sap/bc/adt/packages` collection from a discovery doc.

    7.40 lists only the `settings` service under the Packages workspace.
    """
    return re.sub(
        rb'<app:collection[^>]*href="/sap/bc/adt/packages"[^>]*>.*?</app:collection>',
        b"",
        body,
        flags=re.DOTALL,
    )


class Sap740Proxy:
    """Context manager exposing a local port that emulates 7.40 package routing."""

    def __init__(self, upstream_host, upstream_port,
                 strip_packages_collection=False):
        handler = type(
            "BoundHandler", (_Handler,),
            {
                "upstream_host": upstream_host,
                "upstream_port": upstream_port,
                "strip_packages_collection": strip_packages_collection,
            },
        )
        self._server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self._thread = None

    @property
    def port(self):
        return self._server.server_address[1]

    def __enter__(self):
        self._thread = threading.Thread(
            target=self._server.serve_forever, daemon=True,
        )
        self._thread.start()
        return self

    def __exit__(self, *exc):
        self._server.shutdown()
        self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=10)
        return False
