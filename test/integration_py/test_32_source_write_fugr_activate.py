"""`source write --activate` on the parts of a function group.

A function module and a function group include each have their own ADT URI
below the group (`.../functions/groups/<g>/fmodules/<fm>`,
`.../functions/groups/<g>/includes/<incl>`). `--activate` derives the
activation request's type and name from that URI. It used to map everything
below `functions/groups/` to the group itself (FUGR/F, name = group), so the
request named the group while pointing at the module. (The A4H trial,
SAP_BASIS 7.58, resolves the reference by its URI and activated the right
object anyway, so these tests passed before the fix too; the request content
itself is pinned by the unit tests in test/adt/test_activation.cpp. They guard
the end-to-end behaviour, including that a broken sibling module in the same
group is neither activated nor blocks the activation.)

The test creates a function group in $TMP with one function module and one
include, writes new source to each with `source write --activate`, and checks
independently over ADT REST that the *active* version now holds that source.
"""

import base64
import json
import http.cookiejar
import urllib.error
import urllib.request

import pytest

GROUP = "ZERPL_FG_URI"
FM = "ZERPL_FG_URI_FM"
FM_BROKEN = "ZERPL_FG_URI_FM2"
INCLUDE = "LZERPL_FG_URIF01"
GROUP_URI = f"/sap/bc/adt/functions/groups/{GROUP.lower()}"
FM_URI = f"{GROUP_URI}/fmodules/{FM.lower()}"
INCLUDE_URI = f"{GROUP_URI}/includes/{INCLUDE.lower()}"
FM_BROKEN_URI = f"{GROUP_URI}/fmodules/{FM_BROKEN.lower()}"

pytestmark = [pytest.mark.source, pytest.mark.activation]


def _create_part(adt, kind, name, description):
    """POST a function module / include the way Eclipse ADT does."""
    if kind == "fmodule":
        root, ns, adt_type = ("fmodule:abapFunctionModule",
                              "http://www.sap.com/adt/functions/fmodules", "FUGR/FF")
        content_type = "application/vnd.sap.adt.functions.fmodules.v3+xml"
        path = f"{GROUP_URI}/fmodules"
    else:
        root, ns, adt_type = ("finclude:abapFunctionGroupInclude",
                              "http://www.sap.com/adt/functions/fincludes", "FUGR/I")
        content_type = "application/vnd.sap.adt.functions.fincludes.v2+xml"
        path = f"{GROUP_URI}/includes"
    prefix = root.split(":")[0]
    body = (
        f'<?xml version="1.0" encoding="UTF-8"?>'
        f'<{root} xmlns:{prefix}="{ns}" xmlns:adtcore="http://www.sap.com/adt/core" '
        f'adtcore:description="{description}" adtcore:name="{name}" adtcore:type="{adt_type}">'
        f'<adtcore:containerRef adtcore:name="{GROUP}" adtcore:type="FUGR/F" '
        f'adtcore:uri="{GROUP_URI}"/></{root}>'
    )
    resp = adt.post(path, data=body.encode(), headers={"Content-Type": content_type})
    assert resp.status_code in (200, 201), f"{resp.status_code}: {resp.text[:500]}"


def _active_source(adt, uri):
    resp = adt.get(f"{uri}/source/main?version=active", headers={"Accept": "text/plain"})
    assert resp.status_code == 200, f"{resp.status_code}: {resp.text[:300]}"
    return resp.text


def _write_and_activate(cli, uri, path):
    """Run `source write --activate`; JSON mode prints one document per step."""
    result = cli.run("source", "write", f"{uri}/source/main", "--file", str(path),
                     "--activate", timeout=300)
    assert result.returncode == 0, result.stderr + result.stdout
    return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]


def _delete_group(cli):
    exists = cli.run("object", "read", GROUP_URI)
    if exists.returncode == 0:
        cli.run("object", "delete", GROUP_URI)


class _Response:
    def __init__(self, status_code, text):
        self.status_code = status_code
        self.text = text


class _Adt:
    """ADT REST over the standard library (CSRF token + session cookie),
    independent of the erpl-adt code under test."""

    def __init__(self, cfg):
        self.base = f"http://{cfg['host']}:{cfg['port']}"
        token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
        self.headers = {"Authorization": f"Basic {token}", "sap-client": cfg["client"]}
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.csrf = None

    def _send(self, method, path, data=None, headers=None):
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={**self.headers, **(headers or {})})
        try:
            with self.opener.open(req, timeout=300) as resp:
                return resp.status, dict(resp.headers), resp.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as err:
            return err.code, dict(err.headers), err.read().decode("utf-8", "replace")

    def get(self, path, headers=None):
        status, _, body = self._send("GET", path, headers=headers)
        return _Response(status, body)

    def post(self, path, data, headers=None):
        if self.csrf is None:
            _, hdrs, _ = self._send("GET", "/sap/bc/adt/discovery",
                                    headers={"x-csrf-token": "fetch"})
            self.csrf = {k.lower(): v for k, v in hdrs.items()}.get("x-csrf-token", "")
        status, _, body = self._send("POST", path, data=data,
                                     headers={**(headers or {}), "x-csrf-token": self.csrf})
        return _Response(status, body)


@pytest.fixture(scope="module")
def adt(sap_config):
    return _Adt(sap_config)


@pytest.fixture(scope="module")
def function_group(cli, adt):
    _delete_group(cli)
    cli.run_ok("object", "create", "--type", "FUGR/F", "--name", GROUP,
               "--package", "$TMP", "--description", "erpl-adt source write activate test",
               timeout=300)
    _create_part(adt, "fmodule", FM, "erpl-adt test function module")
    _create_part(adt, "finclude", INCLUDE, "erpl-adt test include")
    _create_part(adt, "fmodule", FM_BROKEN, "erpl-adt test function module, broken")
    yield
    _delete_group(cli)


class TestSourceWriteActivateFunctionGroupParts:

    def test_function_module_source_is_activated(self, cli, adt, function_group, tmp_path):
        source = (
            f"FUNCTION {FM.lower()}.\n"
            "  DATA lv_marker TYPE string.\n"
            "  lv_marker = 'erpl-adt fm v2'.\n"
            "ENDFUNCTION.\n"
        )
        path = tmp_path / "fm.abap"
        path.write_text(source)
        steps = _write_and_activate(cli, FM_URI, path)
        assert steps[-1]["message"] == f"Activated: {FM_URI}"
        assert "erpl-adt fm v2" in _active_source(adt, FM_URI)

    def test_function_group_include_source_is_activated(self, cli, adt, function_group,
                                                        tmp_path):
        source = (
            "FORM zerpl_fg_uri_form.\n"
            "  DATA lv_marker TYPE string.\n"
            "  lv_marker = 'erpl-adt include v2'.\n"
            "ENDFORM.\n"
        )
        path = tmp_path / "incl.abap"
        path.write_text(source)
        steps = _write_and_activate(cli, INCLUDE_URI, path)
        assert steps[-1]["message"] == f"Activated: {INCLUDE_URI}"
        assert "erpl-adt include v2" in _active_source(adt, INCLUDE_URI)

    def test_activates_only_the_module_not_its_group(self, cli, adt, function_group, tmp_path):
        """A colleague's half-finished module in the same group (inactive, with
        a syntax error) must neither block nor be swept into the activation."""
        broken = tmp_path / "broken.abap"
        broken.write_text(f"FUNCTION {FM_BROKEN.lower()}.\n  this is not abap.\nENDFUNCTION.\n")
        result = cli.run("source", "write", f"{FM_BROKEN_URI}/source/main", "--file",
                         str(broken), timeout=300)
        assert result.returncode == 0, result.stderr

        path = tmp_path / "fm.abap"
        path.write_text(f"FUNCTION {FM.lower()}.\n  DATA lv_marker TYPE string.\n"
                        "  lv_marker = 'erpl-adt fm v3'.\nENDFUNCTION.\n")
        steps = _write_and_activate(cli, FM_URI, path)
        assert steps[-1]["message"] == f"Activated: {FM_URI}"
        assert "erpl-adt fm v3" in _active_source(adt, FM_URI)
        assert "this is not abap" not in _active_source(adt, FM_BROKEN_URI)
