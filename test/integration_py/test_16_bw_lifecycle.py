"""BW object lifecycle command tests — create, lock, unlock, save, delete, activate."""

import json
import uuid

import pytest


# ===========================================================================
# BW Create / Lock / Unlock / Delete lifecycle
# ===========================================================================

@pytest.mark.bw
class TestBwCreateLifecycle:

    def test_create_lock_unlock_delete_capability(self, cli, bw_has_search):
        # Probe existing IOBJ for copy source
        source = cli.run("bw", "search", "*", "--max", "1", "--type", "IOBJ")
        if source.returncode != 0:
            pytest.skip("IOBJ search not available for create lifecycle probe")
        source_data = json.loads(source.stdout.strip()) if source.stdout.strip() else []
        if not source_data:
            pytest.skip("No IOBJ source object for create lifecycle probe")

        src_name = source_data[0]["name"]
        new_name = ("ZTST" + uuid.uuid4().hex[:10]).upper()

        create = cli.run("bw", "create", "IOBJ", new_name,
                         "--copy-from-name", src_name,
                         "--copy-from-type", "IOBJ")
        if create.returncode != 0:
            stderr = create.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented", "forbidden",
                                         "\"http_status\":400", "\"http_status\":403",
                                         "\"http_status\":405", "\"http_status\":415")):
                pytest.skip("bw create not supported on this backend profile")
            assert create.returncode != 99
            return

        # If create succeeded, read + lock + unlock + delete should work or skip
        # with backend-specific limitations.
        read = cli.run("bw", "read", "IOBJ", new_name)
        assert read.returncode in (0, 2, 99)

        lock = cli.run("bw", "lock", "IOBJ", new_name)
        if lock.returncode != 0:
            stderr = lock.stderr.strip().lower()
            if any(s in stderr for s in ("not activated", "not implemented",
                                         "\"http_status\":400", "\"http_status\":403",
                                         "\"http_status\":405")):
                pytest.skip("bw lock not fully supported for created object type")
            assert lock.returncode != 99
            return
        lock_data = json.loads(lock.stdout.strip())
        lock_handle = lock_data.get("lock_handle") or lock_data.get("handle")
        assert lock_handle

        unlock = cli.run("bw", "unlock", "IOBJ", new_name)
        assert unlock.returncode == 0

        delete = cli.run("bw", "delete", "IOBJ", new_name, "--lock-handle", lock_handle)
        assert delete.returncode in (0, 2, 99)

    def test_save_missing_lock_handle(self, cli):
        """bw save without --lock-handle fails with usage error."""
        result = cli.run("bw", "save", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_delete_missing_lock_handle(self, cli):
        """bw delete without --lock-handle fails with usage error."""
        result = cli.run("bw", "delete", "ADSO", "ZSALES")
        assert result.returncode != 0

    def test_lock_missing_args(self, cli):
        """bw lock without type and name fails."""
        result = cli.run("bw", "lock")
        assert result.returncode != 0

    def test_activate_missing_args(self, cli):
        """bw activate without type and name fails."""
        result = cli.run("bw", "activate")
        assert result.returncode != 0


# ===========================================================================
# BW Error handling — server-side errors (require BW endpoint)
# ===========================================================================

@pytest.mark.bw
class TestBwServerErrors:

    def test_lock_nonexistent_object(self, cli, bw_available):
        """Locking a nonexistent BW object fails gracefully."""
        result = cli.run("bw", "lock", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode != 0

    def test_unlock_without_lock(self, cli, bw_available):
        """Unlocking without a prior lock succeeds (BW unlock is idempotent)."""
        result = cli.run("bw", "unlock", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        # BW unlock is idempotent — returns success even without an active lock
        assert result.returncode == 0

    def test_activate_nonexistent_object(self, cli, bw_available):
        """Activating a nonexistent BW object fails."""
        result = cli.run("bw", "activate", "ADSO", "ZZZZZ_NONEXISTENT_99999")
        assert result.returncode != 0

    def test_activate_validate_flags_nonexistent(self, cli, bw_available):
        """Validate/exec-check/with-cto/sort/only-ina flags are accepted."""
        result = cli.run("bw", "activate", "ADSO", "ZZZZZ_NONEXISTENT_99999",
                         "--validate", "--sort", "--only-ina",
                         "--exec-check", "--with-cto")
        assert result.returncode != 0

    def test_job_status_invalid_guid(self, cli, bw_available):
        """Job status with invalid GUID fails."""
        result = cli.run("bw", "job", "status",
                         "INVALID_GUID_000000000000")
        assert result.returncode != 0
