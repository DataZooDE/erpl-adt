"""Integration tests for `erpl-adt abapgit cleanup` (helper classes left by
interrupted runs).

Every abapgit command writes a temporary helper class ZCL_ERPLHLP_<8 hex> in
$TMP and deletes it again; a run that is killed leaves it behind. `cleanup`
deletes the logon user's own helpers that are old enough - never a
colleague's, never a fresh one (a run may be using it right now). Here a
leftover is produced the way a debugging user would: --keep-helper.

Independent check: the ADT repository search.
"""

import json
import time
from pathlib import Path

import pytest

SRC = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo" / "src"
FILE = SRC / "zerpl_d_so_status.doma.xml"
PACKAGE = "$TMP"


def _helpers(cli):
    hits = cli.run_ok("search", "ZCL_ERPLHLP_*", "--type", "CLAS/OC", "--max=200")
    return {h["name"] for h in hits}


def _leave_helper(runner, cli):
    before = _helpers(cli)
    runner.run_ok("abapgit", "status", str(FILE), "--package", PACKAGE, "--keep-helper")
    new = _helpers(cli) - before
    assert len(new) == 1, new
    return new.pop()


@pytest.mark.abapgit_deploy
class TestHelperCleanup:

    def test_cleanup_deletes_only_own_old_helpers(self, cli, colleague_cli):
        mine = _leave_helper(cli, cli)
        theirs = _leave_helper(colleague_cli, cli)
        try:
            # Default age (60 min): a fresh helper is kept - a run may be using it.
            fresh = cli.run_ok("abapgit", "cleanup")
            assert mine in fresh["kept"], fresh
            assert mine not in fresh["deleted"]
            assert {mine, theirs} <= _helpers(cli)

            # A typo in the age is refused before anything is touched.
            typo = cli.run("abapgit", "cleanup", "--older-than", "10x")
            assert typo.returncode == 99, typo.stdout + typo.stderr
            assert "10x" in typo.stdout + typo.stderr
            assert mine in _helpers(cli)

            # Once old enough, the own helper goes; the colleague's stays.
            time.sleep(65)
            old = cli.run_ok("abapgit", "cleanup", "--older-than", "1")
            assert mine in old["deleted"], old
            assert theirs not in old["deleted"], old
            assert theirs in old["kept"], old
            left = _helpers(cli)
            assert mine not in left
            assert theirs in left
        finally:
            colleague_cli.run("abapgit", "cleanup", "--older-than", "1")
            for name in {mine, theirs} & _helpers(cli):
                runner = colleague_cli if name == theirs else cli
                runner.run("object", "delete", f"/sap/bc/adt/oo/classes/{name.lower()}")
        assert not ({mine, theirs} & _helpers(cli))
