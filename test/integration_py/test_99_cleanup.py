"""Cleanup sweep — remove any ZTEST_INTEG_* and ZTEST_E2E_* objects left behind."""


class TestCleanup:

    def test_cleanup_test_objects(self, cli):
        """Search for ZTEST_INTEG* and ZTEST_E2E* objects and delete them (best-effort)."""
        import json
        import tempfile

        cleaned = 0
        for pattern in ["ZTEST_INTEG*", "ZTEST_E2E*"]:
            result = cli.run("search", "query", pattern, "--max", "50")
            if result.returncode != 0:
                continue

            data = json.loads(result.stdout) if result.stdout.strip() else []
            if not data:
                continue

            for obj in data:
                uri = obj.get("uri", "")
                if not uri:
                    continue

                try:
                    with tempfile.NamedTemporaryFile(suffix=".json",
                                                     delete=False) as f:
                        sf = f.name
                    lock_result = cli.run("object", "lock", uri,
                                          session_file=sf)
                    if lock_result.returncode == 0:
                        lock_data = json.loads(lock_result.stdout)
                        handle = lock_data.get("handle", "")
                        if handle:
                            del_result = cli.run("object", "delete", uri,
                                                 "--handle", handle,
                                                 session_file=sf)
                            if del_result.returncode == 0:
                                cleaned += 1
                            cli.run("object", "unlock", uri,
                                    "--handle", handle,
                                    session_file=sf)
                except Exception:
                    pass

        if cleaned:
            print(f"Cleaned up {cleaned} test objects")
