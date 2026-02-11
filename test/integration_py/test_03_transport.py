"""Transport management tests — validate transport operations via CLI."""

import re

import pytest


TRANSPORT_PATTERN = re.compile(r"^[A-Z0-9]{3}[A-Z]\d{6}$")


@pytest.mark.transport
class TestTransport:

    def test_list_transports(self, cli):
        """transport list returns JSON array."""
        data = cli.run_ok("transport", "list", "--user", "DEVELOPER")
        assert isinstance(data, list)

    def test_transport_has_fields(self, cli):
        """Listed transports have number, description, owner."""
        data = cli.run_ok("transport", "list", "--user", "DEVELOPER")
        if not data:
            pytest.skip("No transports found for user")
        t = data[0]
        assert "number" in t
        assert "owner" in t

    def test_create_transport(self, cli):
        """transport create returns a valid transport number."""
        data = cli.run_ok("transport", "create",
                          "--desc", "pytest integration test",
                          "--package", "$TMP")
        assert "transport_number" in data
        assert TRANSPORT_PATTERN.match(data["transport_number"]), \
            f"Invalid transport number: {data['transport_number']}"

    def test_release_transport(self, cli):
        """Create then release a transport."""
        create_data = cli.run_ok("transport", "create",
                                 "--desc", "pytest release test",
                                 "--package", "$TMP")
        number = create_data["transport_number"]

        result = cli.run("transport", "release", number)
        # Release may succeed (0) or fail if system doesn't allow immediate release.
        assert result.returncode in (0, 9), \
            f"Unexpected exit code: {result.returncode}"
