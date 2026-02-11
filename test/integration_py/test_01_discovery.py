"""Discovery tests — validate service discovery via CLI."""

import pytest


@pytest.mark.discovery
class TestDiscovery:

    def test_discovery_has_services(self, cli):
        """Discovery returns services list."""
        data = cli.run_ok("discover", "services")
        assert len(data["services"]) > 0

    def test_discovery_service_has_fields(self, cli):
        """Each service entry has title and href."""
        data = cli.run_ok("discover", "services")
        svc = data["services"][0]
        assert "title" in svc
        assert "href" in svc

    def test_discovery_has_capabilities(self, cli):
        """Discovery reports capability flags."""
        data = cli.run_ok("discover", "services")
        assert "has_packages" in data
        assert "has_activation" in data
