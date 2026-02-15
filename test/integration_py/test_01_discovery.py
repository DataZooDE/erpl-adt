"""Discovery tests — validate service discovery via CLI."""

import pytest


@pytest.mark.discovery
class TestDiscovery:

    def test_discovery_has_services(self, cli):
        """Discovery returns workspaces list."""
        data = cli.run_ok("discover", "services")
        assert len(data["workspaces"]) > 0

    def test_discovery_service_has_fields(self, cli):
        """Each workspace has title and services with href."""
        data = cli.run_ok("discover", "services")
        ws = data["workspaces"][0]
        assert "title" in ws
        assert "services" in ws
        svc = ws["services"][0]
        assert "title" in svc
        assert "href" in svc

    def test_discovery_has_capabilities(self, cli):
        """Discovery reports capability flags."""
        data = cli.run_ok("discover", "services")
        assert "has_packages" in data
        assert "has_activation" in data
