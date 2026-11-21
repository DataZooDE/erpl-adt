"""Which object, include and line broke the activation? (issue #90)

An agent generates classes that use each other and activates them in one
request - the only way, since neither compiles alone. When the activation
fails, the agent has to fix the right place. Before #90 the JSON said only
"failed: 1" and the message texts; which of the classes, and whether the
main source or the unit-test include, was left to guess.

Scenario: an order class and a pricing class that uses it, the pricing class
with a unit test whose assertion has a typo. `activate --json order pricing`
must point at the pricing class, its `testclasses` include and the line of
the typo; after fixing exactly that line the same activate is clean.
"""

import json
import random

import pytest

EXIT_ACTIVATION = 5


def order_source(name):
    return (
        f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        "  PUBLIC SECTION.\n"
        "    METHODS net_amount RETURNING VALUE(rv_amount) TYPE i.\n"
        "ENDCLASS.\n\n"
        f"CLASS {name} IMPLEMENTATION.\n"
        "  METHOD net_amount.\n"
        "    rv_amount = 100.\n"
        "  ENDMETHOD.\n"
        "ENDCLASS.\n"
    )


def pricing_source(name, order):
    return (
        f"CLASS {name} DEFINITION PUBLIC FINAL CREATE PUBLIC.\n"
        "  PUBLIC SECTION.\n"
        "    METHODS gross_amount IMPORTING io_order TYPE REF TO "
        f"{order.lower()}\n"
        "                         RETURNING VALUE(rv_amount) TYPE i.\n"
        "ENDCLASS.\n\n"
        f"CLASS {name} IMPLEMENTATION.\n"
        "  METHOD gross_amount.\n"
        "    rv_amount = io_order->net_amount( ) * 119 / 100.\n"
        "  ENDMETHOD.\n"
        "ENDCLASS.\n"
    )


# Line 9 creates the object under test; the broken variant misspells its
# class name there.
BROKEN_LINE = 9


def pricing_tests(pricing, order, broken):
    target = f"{pricing.lower()}x" if broken else pricing.lower()
    return (
        "CLASS ltcl_pricing DEFINITION FINAL FOR TESTING\n"
        "  DURATION SHORT RISK LEVEL HARMLESS.\n"
        "  PRIVATE SECTION.\n"
        "    METHODS adds_vat FOR TESTING.\n"
        "ENDCLASS.\n\n"
        "CLASS ltcl_pricing IMPLEMENTATION.\n"
        "  METHOD adds_vat.\n"
        f"    DATA(lo_cut) = NEW {target}( ).\n"
        "    cl_abap_unit_assert=>assert_equals(\n"
        f"      act = lo_cut->gross_amount( NEW {order.lower()}( ) ) exp = 119 ).\n"
        "  ENDMETHOD.\n"
        "ENDCLASS.\n"
    )


@pytest.fixture
def two_classes(cli):
    suffix = random.randint(10000, 99999)
    names = {"order": f"ZCL_ERPL_ORD_{suffix}", "pricing": f"ZCL_ERPL_PRC_{suffix}"}
    uris = {}
    for role, name in names.items():
        data = cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", name,
                          "--package", "$TMP", "--description", f"#90 {role}")
        uris[role] = data.get("uri", f"/sap/bc/adt/oo/classes/{name.lower()}")
    yield names, uris
    for uri in reversed(list(uris.values())):
        cli.run("object", "delete", uri)


def write(cli, uri, source, tmp_path):
    path = tmp_path / "src.abap"
    path.write_text(source)
    cli.run_ok("source", "write", uri, "--file", str(path))


@pytest.mark.activation
@pytest.mark.slow
class TestActivationNamesTheBrokenPlace:

    def test_points_at_class_include_and_line(self, cli, two_classes, tmp_path):
        names, uris = two_classes
        order, pricing = names["order"], names["pricing"]
        write(cli, uris["order"] + "/source/main", order_source(order), tmp_path)
        write(cli, uris["pricing"] + "/source/main", pricing_source(pricing, order), tmp_path)
        write(cli, uris["pricing"] + "/includes/testclasses",
              pricing_tests(pricing, order, broken=True), tmp_path)

        result = cli.run("activate", uris["order"], uris["pricing"])
        assert result.returncode == EXIT_ACTIVATION, result.stdout + result.stderr
        data = json.loads(result.stdout)
        assert data["error_messages"], data  # the text list stays (compatibility)

        errors = [m for m in data.get("messages", []) if m.get("type") in ("E", "A")]
        assert errors, f"no structured error message: {data}"
        first = errors[0]
        assert first["object"] == pricing, first
        assert first["include"] == "testclasses", first
        assert first["line"] == BROKEN_LINE, first
        assert first["column"] > 0, first
        assert first["text"], first
        assert not [m for m in errors if m.get("object") == order], errors

        # A human reads the same place, compiler style, in text mode.
        text = cli.run_no_json("activate", uris["order"], uris["pricing"])
        assert text.returncode == EXIT_ACTIVATION
        assert f"{pricing} testclasses:{BROKEN_LINE}:" in text.stderr, text.stderr

        # source write --activate reports the same place.
        path = tmp_path / "t.abap"
        path.write_text(pricing_tests(pricing, order, broken=True))
        written = cli.run("source", "write", uris["pricing"] + "/includes/testclasses",
                          "--file", str(path), "--activate")
        assert written.returncode == EXIT_ACTIVATION, written.stdout + written.stderr
        msgs = [m for m in json.loads(written.stdout)["activation"]["messages"] if m["type"] == "E"]
        assert msgs and msgs[0]["include"] == "testclasses" and msgs[0]["line"] == BROKEN_LINE, msgs

        # The agent fixes exactly that line and activates again.
        write(cli, uris["pricing"] + "/includes/testclasses",
              pricing_tests(pricing, order, broken=False), tmp_path)
        fixed = cli.run("activate", uris["order"], uris["pricing"])
        assert fixed.returncode == 0, fixed.stdout + fixed.stderr
        assert json.loads(fixed.stdout)["failed"] == 0
