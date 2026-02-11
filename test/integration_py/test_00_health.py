"""Health check tests — verify basic connectivity and CLI operation."""

import json
import os
import socket
import subprocess

import pytest


@pytest.mark.smoke
class TestHealth:

    def test_tcp_connect(self, sap_config):
        """TCP connection to SAP port succeeds."""
        with socket.create_connection(
            (sap_config["host"], sap_config["port"]), timeout=10
        ):
            pass

    def test_discover_services(self, cli):
        """CLI discover services command succeeds."""
        data = cli.run_ok("discover", "services")
        assert "services" in data
        assert isinstance(data["services"], list)

    def test_discover_has_services(self, cli):
        """Discovery returns at least one service."""
        data = cli.run_ok("discover", "services")
        assert len(data["services"]) > 0

    def test_bad_credentials_fail(self, cli):
        """CLI with bad credentials returns non-zero exit code."""
        bad_result = subprocess.run(
            [cli.binary,
             "--host", cli.base_args[cli.base_args.index("--host") + 1],
             "--port", cli.base_args[cli.base_args.index("--port") + 1],
             "--user", "BADUSER",
             "--password", "BADPASS",
             "--client", cli.base_args[cli.base_args.index("--client") + 1],
             "--json=true",
             "discover", "services"],
            capture_output=True, text=True, timeout=30,
        )
        assert bad_result.returncode != 0

    def test_version_flag(self, cli):
        """--version prints version string and exits 0."""
        result = cli.run_raw("--version")
        assert result.returncode == 0
        assert "erpl-adt" in result.stdout
        assert "0." in result.stdout  # version starts with 0.x

    def test_help_output(self, cli):
        """Running with just a group and no action prints help with command list."""
        result = cli.run_raw("search")
        assert result.returncode == 0  # group-only prints help and exits 0
        combined = result.stdout + result.stderr
        assert "search" in combined
        assert "Actions:" in combined or "Available commands" in combined or "Usage" in combined

    def test_password_env(self, cli, sap_config):
        """--password-env reads password from environment variable."""
        env = os.environ.copy()
        env["MY_TEST_PASSWORD"] = sap_config["password"]
        result = cli.run_raw(
            "--host", sap_config["host"],
            "--port", str(sap_config["port"]),
            "--user", sap_config["user"],
            "--client", sap_config["client"],
            "--password-env", "MY_TEST_PASSWORD",
            "--json=true",
            "discover", "services",
            env=env,
        )
        assert result.returncode == 0
        data = json.loads(result.stdout)
        assert "services" in data

    def test_verbose_flag(self, cli, sap_config):
        """-v flag produces INFO-level log output on stderr."""
        result = cli.run_raw(
            "-v",
            "--host", sap_config["host"],
            "--port", str(sap_config["port"]),
            "--user", sap_config["user"],
            "--password", sap_config["password"],
            "--client", sap_config["client"],
            "--json=true",
            "discover", "services",
        )
        assert result.returncode == 0
        assert "[INFO]" in result.stderr

    def test_debug_verbose_flag(self, cli, sap_config):
        """-vv flag produces DEBUG-level log output on stderr."""
        result = cli.run_raw(
            "-vv",
            "--host", sap_config["host"],
            "--port", str(sap_config["port"]),
            "--user", sap_config["user"],
            "--password", sap_config["password"],
            "--client", sap_config["client"],
            "--json=true",
            "discover", "services",
        )
        assert result.returncode == 0
        assert "[DEBUG]" in result.stderr


@pytest.mark.smoke
class TestLoginLogout:

    def test_login_saves_creds(self, cli, sap_config, tmp_path):
        """login command creates .adt.creds file with correct content."""
        result = subprocess.run(
            [cli.binary, "login",
             "--host", sap_config["host"],
             "--port", str(sap_config["port"]),
             "--user", sap_config["user"],
             "--password", sap_config["password"],
             "--client", sap_config["client"]],
            capture_output=True, text=True, timeout=10,
            cwd=str(tmp_path),
        )
        assert result.returncode == 0
        creds_file = tmp_path / ".adt.creds"
        assert creds_file.exists()
        data = json.loads(creds_file.read_text())
        assert data["host"] == sap_config["host"]
        assert data["port"] == sap_config["port"]
        assert data["user"] == sap_config["user"]
        assert data["password"] == sap_config["password"]
        assert data["client"] == sap_config["client"]

    def test_login_file_permissions(self, cli, sap_config, tmp_path):
        """login creates .adt.creds with 600 permissions."""
        subprocess.run(
            [cli.binary, "login",
             "--host", sap_config["host"],
             "--port", str(sap_config["port"]),
             "--user", sap_config["user"],
             "--password", sap_config["password"],
             "--client", sap_config["client"]],
            capture_output=True, text=True, timeout=10,
            cwd=str(tmp_path),
        )
        creds_file = tmp_path / ".adt.creds"
        mode = creds_file.stat().st_mode & 0o777
        assert mode == 0o600, f"Expected 600, got {oct(mode)}"

    def test_login_then_discover(self, cli, sap_config, tmp_path):
        """After login, commands work without explicit credentials."""
        # Login
        subprocess.run(
            [cli.binary, "login",
             "--host", sap_config["host"],
             "--port", str(sap_config["port"]),
             "--user", sap_config["user"],
             "--password", sap_config["password"],
             "--client", sap_config["client"]],
            capture_output=True, text=True, timeout=10,
            cwd=str(tmp_path),
        )
        # Discover without connection flags — should read from .adt.creds
        env = os.environ.copy()
        env.pop("SAP_PASSWORD", None)
        result = subprocess.run(
            [cli.binary, "--json=true", "discover", "services"],
            capture_output=True, text=True, timeout=30,
            cwd=str(tmp_path), env=env,
        )
        assert result.returncode == 0, f"stderr: {result.stderr}"
        data = json.loads(result.stdout)
        assert "services" in data

    def test_logout_deletes_creds(self, cli, tmp_path):
        """logout command deletes .adt.creds file."""
        # Create a dummy creds file.
        creds_file = tmp_path / ".adt.creds"
        creds_file.write_text('{"host":"x"}')
        result = subprocess.run(
            [cli.binary, "logout"],
            capture_output=True, text=True, timeout=10,
            cwd=str(tmp_path),
        )
        assert result.returncode == 0
        assert not creds_file.exists()

    def test_logout_without_creds_succeeds(self, cli, tmp_path):
        """logout succeeds even when no .adt.creds exists."""
        result = subprocess.run(
            [cli.binary, "logout"],
            capture_output=True, text=True, timeout=10,
            cwd=str(tmp_path),
        )
        assert result.returncode == 0
