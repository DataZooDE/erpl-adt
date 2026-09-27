"""Issue #64: create RAP repository objects through the public CLI on SAP."""

import base64
import urllib.request
import xml.etree.ElementTree as ET
from uuid import uuid4

import pytest


def create(cli, object_type, name, *extra):
    return cli.run_ok(
        "object", "create", "--type", object_type, "--name", name,
        "--package", "$TMP", "--description", "RAP creation regression", *extra,
    )["uri"]


@pytest.mark.object
@pytest.mark.parametrize("object_type", ["BDEF/BDO", "SRVD/SRV"])
def test_create_rap_object(cli, object_type):
    name = "ZADT64_" + uuid4().hex[:12].upper()
    uri = create(cli, object_type, name)
    try:
        obj = cli.run_ok("object", "read", uri)
        assert obj["name"].upper() == name
        assert obj["type"] == object_type
        # The new object exposes the normal source endpoint.
        assert "source" in cli.run_ok("source", "read", uri + "/source/main")
    finally:
        cli.run_ok("object", "delete", uri)


@pytest.fixture
def rap_service(cli, tmp_path):
    name = "ZADT64_" + uuid4().hex[:12].upper()
    uri = create(cli, "SRVD/SRV", name)
    try:
        source = tmp_path / "service.asrvd"
        source.write_text(f"define service {name} {{ expose I_Currency as Currency; }}\n")
        cli.run_ok("source", "write", uri + "/source/main", "--file", source)
        activation = cli.run_ok("activate", uri)
        assert activation["failed"] == 0, activation
        assert cli.run_ok("object", "read", uri)["version"] == "active"
        yield name
    finally:
        cli.run_ok("object", "delete", uri)


@pytest.mark.object
@pytest.mark.parametrize("version,category", [("V2", "0"), ("V2", "1"),
                                              ("V4", "0"), ("V4", "1")])
def test_create_rap_binding(cli, rap_service, version, category):
    name = "ZADT64_" + uuid4().hex[:12].upper()
    uri = create(cli, "SRVB/SVB", name,
                 "--binding-type", "ODATA", "--binding-version", version,
                 "--binding-category", category, "--service-definition", rap_service)
    try:
        obj = cli.run_ok("object", "read", uri)
        assert obj["name"].upper() == name
        assert obj["type"] == "SRVB/SVB"
        activation = cli.run_ok("activate", uri)
        assert activation["failed"] == 0, activation
        assert cli.run_ok("object", "read", uri)["version"] == "active"
        # Read SAP's persisted XML independently: object read intentionally exposes
        # only common metadata, not the binding-specific fields being regressed.
        auth = base64.b64encode(f"{cli.user}:{cli.password}".encode()).decode()
        request = urllib.request.Request(
            f"http://{cli.host}:{cli.port}{uri}",
            headers={"Authorization": f"Basic {auth}", "sap-client": cli.client,
                     "Accept": "application/vnd.sap.adt.businessservices.servicebinding.v2+xml"},
        )
        with urllib.request.urlopen(request, timeout=30) as response:
            root = ET.fromstring(response.read())
        srvb = "{http://www.sap.com/adt/ddic/ServiceBindings}"
        adt = "{http://www.sap.com/adt/core}"
        binding = root.find(srvb + "binding")
        assert binding is not None
        assert binding.attrib[srvb + "type"] == "ODATA"
        assert binding.attrib[srvb + "version"] == version
        assert binding.attrib[srvb + "category"] == category
        definition = root.find(".//" + srvb + "serviceDefinition")
        assert definition is not None
        assert definition.attrib[adt + "name"].upper() == rap_service
    finally:
        cli.run_ok("object", "delete", uri)
