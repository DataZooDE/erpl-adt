"""Package existence must be determinable on every SAP_BASIS release.

Regression tests for GitHub issue #35.

On SAP_BASIS 7.40 there is no `/sap/bc/adt/packages/<name>` object resource, so
the probe erpl-adt used as its existence oracle answered 404 for every package.
That 404 was mapped to `exists: false` (`package exists`) and to
"Package X does not exist" (`package list` of a childless package) — confident
false negatives rather than errors.

Each behaviour is asserted twice:

* through `cli_740`, a proxy in front of the live SAP system that 404s the
  per-package resource exactly the way a 7.40 ICF tree does; and
* through `cli`, straight at the ABAP Cloud trial,

so the fix is proven on 7.40 and guarded against regression on 7.5x+.
"""

import pytest

pytestmark = pytest.mark.packages

# A standard SAP package, present on every system.
STANDARD_PACKAGE = "SABAPDEMOS"
BOGUS_PACKAGE = "ZNONEXISTENT_PKG_99999"


class TestPackageExistsOn740:
    """`package exists` must not answer from the package resource alone."""

    def test_standard_package_exists(self, cli_740):
        """A populated SAP package exists even with no package resource."""
        data = cli_740.run_ok("package", "exists", STANDARD_PACKAGE)
        assert data["exists"] is True, (
            "package exists returned a false negative — the 404 from the "
            "missing 7.40 package resource was taken as proof of absence"
        )

    def test_local_package_exists(self, cli_740):
        """$-local packages resolve through the fallback oracle too."""
        data = cli_740.run_ok("package", "exists", "$TMP")
        assert data["exists"] is True

    def test_empty_package_exists(self, cli_740, empty_package):
        """An existing but childless package exists."""
        data = cli_740.run_ok("package", "exists", empty_package)
        assert data["exists"] is True

    def test_bogus_package_does_not_exist(self, cli_740):
        """The fallback must not overcorrect into false positives."""
        data = cli_740.run_ok("package", "exists", BOGUS_PACKAGE)
        assert data["exists"] is False

    def test_result_reports_how_it_was_resolved(self, cli_740):
        """Callers can tell "absent" from "could not determine"."""
        data = cli_740.run_ok("package", "exists", STANDARD_PACKAGE)
        assert data["resolved_via"] == "search"


class TestPackageListOn740:
    """`package list` must distinguish "empty" from "non-existent"."""

    def test_empty_package_lists_as_empty(self, cli_740, empty_package):
        """An existing empty package yields [] and exit code 0."""
        data = cli_740.run_ok("package", "list", empty_package)
        assert data == []

    def test_missing_package_still_reports_not_found(self, cli_740):
        """A genuinely absent package must still fail with exit code 2."""
        result = cli_740.run_fail("package", "list", BOGUS_PACKAGE)
        assert result.returncode == 2

    def test_populated_package_lists_contents(self, cli_740):
        """Listing is unaffected for packages that do have children."""
        data = cli_740.run_ok("package", "list", STANDARD_PACKAGE)
        assert len(data) > 0


class TestPackageExistenceOnCloud:
    """The same answers must come back on 7.5x+ / ABAP Cloud."""

    def test_standard_package_exists(self, cli):
        data = cli.run_ok("package", "exists", STANDARD_PACKAGE)
        assert data["exists"] is True
        assert data["resolved_via"] == "package_resource"

    def test_bogus_package_does_not_exist(self, cli):
        data = cli.run_ok("package", "exists", BOGUS_PACKAGE)
        assert data["exists"] is False

    def test_empty_package_lists_as_empty(self, cli, empty_package):
        assert cli.run_ok("package", "list", empty_package) == []

    def test_missing_package_reports_not_found(self, cli):
        result = cli.run_fail("package", "list", BOGUS_PACKAGE)
        assert result.returncode == 2


class TestDiscoveryPackagesCapability:
    """`has_packages` must reflect the collection, not any /packages substring."""

    def test_capability_false_without_packages_collection(
            self, cli_740_no_collection):
        """7.40 lists only /packages/settings — that is not package support."""
        data = cli_740_no_collection.run_ok("discover", "services")
        assert data["has_packages"] is False

    def test_capability_true_with_packages_collection(self, cli):
        data = cli.run_ok("discover", "services")
        assert data["has_packages"] is True
