"""E2E Scenario 8: BW Lock/Unlock Stateful Workflow.

Simulates the fundamental agent editing pattern — lock an object, inspect it,
then release the lock using --session-file to persist stateful session state:
  discover -> search (find IOBJ) -> read -> lock -> read (while locked) -> unlock -> verify lock gone

Marked slow: mutating operations (takes/releases a BW object lock).
"""

import json

import pytest


@pytest.mark.e2e
@pytest.mark.bw
@pytest.mark.slow
class TestBwLockLifecycle:
    """BW object lock lifecycle — the agent editing pattern."""

    @pytest.fixture(scope="class", autouse=True)
    def bw_lock_context(self, cli, tmp_path_factory):
        """Class-scoped context for lock lifecycle.

        Provides session file and ensures best-effort unlock on teardown.
        """
        tmp = tmp_path_factory.mktemp("bw_lock")
        ctx = {
            "session_file": str(tmp / "session.json"),
            "obj_type": None,
            "obj_name": None,
            "locked": False,
        }
        self.__class__._ctx = ctx
        yield ctx
        # Best-effort unlock on teardown
        if ctx.get("locked") and ctx.get("obj_type") and ctx.get("obj_name"):
            try:
                cli.run("bw", "unlock", ctx["obj_type"], ctx["obj_name"],
                        session_file=ctx["session_file"])
            except Exception:
                pass

    @pytest.mark.order(1)
    def test_step1_gate_find_lockable_object(self, cli):
        """Step 1: Gate — discover BW and find a lockable IOBJ."""
        result = cli.run("bw", "discover")
        if result.returncode != 0:
            pytest.skip("BW Modeling API not available")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not data:
            pytest.skip("BW discovery returned no services")

        terms = {s.get("term", "") for s in data}
        has_search = "bwSearch" in terms or "search" in terms
        if not has_search:
            pytest.skip("BW search not available")

        # Search for IOBJs — they are lightweight and commonly lockable
        result = cli.run("bw", "search", "*", "--max", "5", "--type", "IOBJ")
        if result.returncode != 0:
            pytest.skip("BW search failed")
        objs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        if not objs:
            # Fallback: try any object type
            result = cli.run("bw", "search", "*", "--max", "5")
            if result.returncode != 0:
                pytest.skip("BW search failed")
            objs = json.loads(result.stdout.strip()) if result.stdout.strip() else []
            if not objs:
                pytest.skip("No BW objects found for lock test")

        ctx = self.__class__._ctx
        ctx["obj_type"] = objs[0]["type"]
        ctx["obj_name"] = objs[0]["name"]
        self.__class__.gate_passed = True

    @pytest.mark.order(2)
    def test_step2_read_pre_lock(self, cli):
        """Step 2: Read the object before locking."""
        if not getattr(self.__class__, "gate_passed", False):
            pytest.skip("Gate step did not pass")
        ctx = self.__class__._ctx
        data = cli.run_ok("bw", "read", ctx["obj_type"], ctx["obj_name"])
        assert data["name"] == ctx["obj_name"]
        assert data["type"] == ctx["obj_type"]

    @pytest.mark.order(3)
    def test_step3_lock_object(self, cli):
        """Step 3: Lock the object with --session-file."""
        if not getattr(self.__class__, "gate_passed", False):
            pytest.skip("Gate step did not pass")
        ctx = self.__class__._ctx
        result = cli.run("bw", "lock", ctx["obj_type"], ctx["obj_name"],
                         session_file=ctx["session_file"])
        if result.returncode != 0:
            # BW lock may fail for many reasons on trial systems:
            # - CSRF/403: stateful session not established
            # - 400: lock service rejects the request
            # - 404: endpoint not activated
            # - Lock conflict: object locked by another user
            # All are graceful skips — we can't lock on this system.
            pytest.skip(f"BW lock not available: {result.stderr.strip()[:200]}")
        data = json.loads(result.stdout.strip())
        assert "lock_handle" in data or "handle" in data, \
            f"Lock response missing handle: {data}"
        ctx["locked"] = True

    @pytest.mark.order(4)
    def test_step4_read_while_locked(self, cli):
        """Step 4: Object is still readable while locked."""
        if not getattr(self.__class__, "gate_passed", False):
            pytest.skip("Gate step did not pass")
        ctx = self.__class__._ctx
        if not ctx.get("locked"):
            pytest.skip("Lock step did not succeed")
        data = cli.run_ok("bw", "read", ctx["obj_type"], ctx["obj_name"])
        assert data["name"] == ctx["obj_name"]

    @pytest.mark.order(5)
    def test_step5_unlock_object(self, cli):
        """Step 5: Unlock the object (clean release)."""
        if not getattr(self.__class__, "gate_passed", False):
            pytest.skip("Gate step did not pass")
        ctx = self.__class__._ctx
        if not ctx.get("locked"):
            pytest.skip("Lock step did not succeed")
        result = cli.run("bw", "unlock", ctx["obj_type"], ctx["obj_name"],
                         session_file=ctx["session_file"])
        assert result.returncode == 0, \
            f"bw unlock failed: {result.stderr.strip()}"
        ctx["locked"] = False

    @pytest.mark.order(6)
    def test_step6_verify_lock_released(self, cli):
        """Step 6: Verify lock is gone via locks list."""
        if not getattr(self.__class__, "gate_passed", False):
            pytest.skip("Gate step did not pass")
        ctx = self.__class__._ctx
        result = cli.run("bw", "locks", "list",
                         "--search", ctx["obj_name"])
        if result.returncode != 0:
            # locks list may not be available — skip verification
            pytest.skip("bw locks list not available for verification")
        data = json.loads(result.stdout.strip()) if result.stdout.strip() else []
        # After unlock, our object should not appear in the lock list
        assert isinstance(data, list)
        # If there are locks, none should be for our object
        for lock in data:
            lock_str = json.dumps(lock).lower()
            assert ctx["obj_name"].lower() not in lock_str, \
                f"Lock still present for {ctx['obj_name']}: {lock}"
