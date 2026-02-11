"""Locking tests — validate lock/unlock operations via CLI."""

import pytest


@pytest.mark.locking
class TestLocking:

    def test_lock_returns_handle(self, test_class, cli, session_file):
        """object lock returns a lock handle."""
        uri = test_class["uri"]
        data = cli.run_ok("object", "lock", uri, session_file=session_file)
        assert "handle" in data
        assert data["handle"], "Lock handle is empty"

        # Cleanup: unlock.
        cli.run("object", "unlock", uri,
                "--handle", data["handle"],
                session_file=session_file)

    def test_lock_has_transport_info(self, test_class, cli, session_file):
        """Lock response includes transport information."""
        uri = test_class["uri"]
        data = cli.run_ok("object", "lock", uri, session_file=session_file)
        assert "transport_number" in data
        assert "transport_owner" in data

        # Cleanup.
        cli.run("object", "unlock", uri,
                "--handle", data["handle"],
                session_file=session_file)

    def test_unlock_succeeds(self, test_class, cli, session_file):
        """Lock then unlock completes without error."""
        uri = test_class["uri"]
        lock_data = cli.run_ok("object", "lock", uri,
                               session_file=session_file)
        handle = lock_data["handle"]

        # Unlock should succeed.
        cli.run_ok("object", "unlock", uri,
                   "--handle", handle,
                   session_file=session_file)

    def test_lock_creates_session_file(self, test_class, cli, session_file):
        """Lock with --session-file creates a session file on disk."""
        import os
        uri = test_class["uri"]
        data = cli.run_ok("object", "lock", uri, session_file=session_file)

        assert os.path.isfile(session_file), "Session file not created"

        # Cleanup: use handle from the lock above.
        cli.run("object", "unlock", uri,
                "--handle", data["handle"],
                session_file=session_file)
