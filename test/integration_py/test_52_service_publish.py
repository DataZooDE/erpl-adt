"""Issue #104: publish and unpublish an OData V2 service binding through the CLI.

A CDS view entity over TADIR (cross-client, present on every ABAP system), a
service definition exposing it and a V2 binding over that, all in $TMP. After
activation the service has no endpoint yet; `service publish` gives it one
(`GET /sap/opu/odata/sap/<BINDING>/$metadata` answers 200), `service unpublish`
takes it away again. Everything is deleted afterwards.
"""

import base64
import time
import urllib.error
import urllib.request
from uuid import uuid4

import pytest


def _create(cli, object_type, name, *extra):
    return cli.run_ok(
        "object", "create", "--type", object_type, "--name", name,
        "--package", "$TMP", "--description", "erpl-adt #104 publish", *extra,
    )["uri"]


def _write_and_activate(cli, uri, path, source):
    path.write_text(source)
    cli.run_ok("source", "write", uri + "/source/main", "--file", path)
    activation = cli.run_ok("activate", uri)
    assert activation["failed"] == 0, activation


def _metadata_status(cli, service, path=None):
    """HTTP status of the service's $metadata (the gateway, not ADT); path: the V4 service path instead of V2's."""
    auth = base64.b64encode(f"{cli.user}:{cli.password}".encode()).decode()
    base = path or f"/sap/opu/odata/sap/{service}"
    request = urllib.request.Request(
        f"http://{cli.host}:{cli.port}{base}/$metadata"
        f"?sap-client={cli.client}",
        headers={"Authorization": f"Basic {auth}", "Accept": "application/xml"},
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return response.status
    except urllib.error.HTTPError as e:
        return e.code


def _wait_for_metadata(cli, service, published, attempts=10, path=None):
    """Poll until the endpoint is up (200) or down again (anything else: A4H answers 403 for a service that is
    not published, other systems 404)."""
    status = None
    for _ in range(attempts):
        status = _metadata_status(cli, service, path)
        if (status == 200) == published:
            return status
        time.sleep(2)
    return status


@pytest.fixture
def v2_binding(cli, tmp_path):
    """An activated V2 binding (category 1, Web API) over a view entity on TADIR."""
    yield from _binding(cli, tmp_path, "V2")


@pytest.fixture
def v4_binding(cli, tmp_path):
    """An activated V4 binding (category 1, Web API): yields (binding, service definition)."""
    yield from _binding(cli, tmp_path, "V4", with_definition=True)


def _binding(cli, tmp_path, version, with_definition=False):
    suffix = uuid4().hex[:8].upper()
    view = "ZI_ERPL104_" + suffix
    service_def = "ZERPL104_SD_" + suffix
    binding = ("ZERPL104_O2_" if version == "V2" else "ZERPL104_O4_") + suffix
    created = []
    try:
        view_uri = _create(cli, "DDLS/DF", view)
        created.append(view_uri)
        _write_and_activate(cli, view_uri, tmp_path / "view.ddls", (
            "@AccessControl.authorizationCheck: #NOT_REQUIRED\n"
            "@EndUserText.label: 'erpl-adt #104 publish'\n"
            f"define view entity {view} as select from tadir {{\n"
            "  key pgmid    as ProgramId,\n"
            "  key object   as ObjectType,\n"
            "  key obj_name as ObjectName,\n"
            "      devclass as PackageName\n"
            "}\n"
        ))

        sd_uri = _create(cli, "SRVD/SRV", service_def)
        created.append(sd_uri)
        _write_and_activate(cli, sd_uri, tmp_path / "service.asrvd",
                            f"define service {service_def} {{ expose {view} as Objects; }}\n")

        binding_uri = _create(cli, "SRVB/SVB", binding,
                              "--binding-type", "ODATA", "--binding-version", version,
                              "--binding-category", "1", "--service-definition", service_def)
        created.append(binding_uri)
        activation = cli.run_ok("activate", binding_uri)
        assert activation["failed"] == 0, activation
        yield (binding, service_def) if with_definition else binding
    finally:
        # A binding that is still published cannot always be deleted; take the
        # service down first (harmless when it was never published).
        cli.run("service", "unpublish", binding, "--odata", version.lower())
        for uri in reversed(created):
            cli.run("object", "delete", uri)


@pytest.mark.object
@pytest.mark.slow
def test_publish_and_unpublish_v2_binding(cli, v2_binding):
    # Activated, not published: the gateway does not know the service.
    assert _metadata_status(cli, v2_binding) != 200

    published = cli.run_ok("service", "publish", v2_binding)
    assert published["ok"] is True, published
    assert published["severity"] == "OK", published
    assert published["binding"] == v2_binding
    assert published["odata"] == "v2"
    assert published["service_version"] == "0001"
    assert published["action"] == "publish"

    assert _wait_for_metadata(cli, v2_binding, published=True) == 200

    unpublished = cli.run_ok("service", "unpublish", v2_binding, "--service-version", "0001",
                             "--odata", "v2")
    assert unpublished["ok"] is True, unpublished
    assert unpublished["severity"] == "OK", unpublished
    assert unpublished["action"] == "unpublish"

    assert _wait_for_metadata(cli, v2_binding, published=False) != 200


@pytest.mark.object
def test_publish_unknown_binding_fails(cli):
    # A binding that does not exist: an HTTP error or a non-OK severity, never exit 0.
    name = "ZERPL104_NOPE_" + uuid4().hex[:6].upper()
    result = cli.run("service", "publish", name)
    assert result.returncode != 0, result.stdout


def test_publish_and_unpublish_v4_binding(cli, v4_binding):
    # a Web API V4 binding answers under /sap/opu/odata4/sap/<binding>/srvd_a2x/sap/<service definition>/0001
    binding, service_def = v4_binding
    path = f"/sap/opu/odata4/sap/{binding.lower()}/srvd_a2x/sap/{service_def.lower()}/0001"
    assert _metadata_status(cli, binding, path) != 200
    published = cli.run_ok("service", "publish", binding, "--odata", "v4")
    assert published["severity"] == "OK", published
    assert _wait_for_metadata(cli, binding, published=True, path=path) == 200
    unpublished = cli.run_ok("service", "unpublish", binding, "--odata", "v4")
    assert unpublished["severity"] == "OK", unpublished
    assert _wait_for_metadata(cli, binding, published=False, path=path) != 200
