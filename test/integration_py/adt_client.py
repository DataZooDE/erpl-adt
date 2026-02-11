"""Minimal ADT REST client for integration testing.

Wraps requests.Session with:
- Basic auth + sap-client header
- CSRF token fetch and auto-refresh on 403
- Stateful session support (sap-contextid)
"""

import requests
import urllib3

# Suppress InsecureRequestWarning for self-signed certs.
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


class AdtClient:
    """HTTP client for SAP ADT REST API."""

    def __init__(self, host, port, user, password, client="001", https=False):
        scheme = "https" if https else "http"
        self.base_url = f"{scheme}://{host}:{port}"
        self.session = requests.Session()
        self.session.auth = (user, password)
        self.session.headers.update({
            "sap-client": client,
            "Accept": "application/xml",
        })
        self.session.verify = False
        self.csrf_token = None

    def fetch_csrf(self):
        """Fetch CSRF token from the discovery endpoint."""
        resp = self.session.get(
            f"{self.base_url}/sap/bc/adt/discovery",
            headers={"x-csrf-token": "fetch"},
        )
        self.csrf_token = resp.headers.get("x-csrf-token", "")
        return self.csrf_token

    def _ensure_csrf(self):
        if not self.csrf_token:
            self.fetch_csrf()

    def _csrf_headers(self, extra=None):
        headers = {"x-csrf-token": self.csrf_token or ""}
        if extra:
            headers.update(extra)
        return headers

    def _retry_on_403(self, method, path, **kwargs):
        """Execute request; on 403, re-fetch CSRF and retry once."""
        url = f"{self.base_url}{path}"
        resp = method(url, **kwargs)
        if resp.status_code == 403:
            self.fetch_csrf()
            kwargs.setdefault("headers", {})["x-csrf-token"] = self.csrf_token
            resp = method(url, **kwargs)
        return resp

    def get(self, path, **kwargs):
        """GET request (no CSRF needed)."""
        return self.session.get(f"{self.base_url}{path}", **kwargs)

    def post(self, path, data=None, headers=None, **kwargs):
        """POST request with CSRF token and auto-retry on 403."""
        self._ensure_csrf()
        h = self._csrf_headers(headers)
        return self._retry_on_403(self.session.post, path, data=data, headers=h, **kwargs)

    def put(self, path, data=None, headers=None, **kwargs):
        """PUT request with CSRF token and auto-retry on 403."""
        self._ensure_csrf()
        h = self._csrf_headers(headers)
        return self._retry_on_403(self.session.put, path, data=data, headers=h, **kwargs)

    def delete(self, path, headers=None, **kwargs):
        """DELETE request with CSRF token and auto-retry on 403."""
        self._ensure_csrf()
        h = self._csrf_headers(headers)
        return self._retry_on_403(self.session.delete, path, headers=h, **kwargs)

    def enable_stateful(self):
        """Enable stateful session mode (required for locking)."""
        self.session.headers["sap-contextid"] = ""

    def disable_stateful(self):
        """Disable stateful session mode."""
        self.session.headers.pop("sap-contextid", None)

    def close(self):
        """Close the underlying requests session."""
        self.session.close()
