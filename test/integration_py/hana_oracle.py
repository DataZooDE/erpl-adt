"""Read-only HANA cross-checks against the SAP system under test.

Some facts have no ADT endpoint (dynpro generation flags, system variants,
transaction tables). Tests read them straight from the database, which is
independent of erpl-adt's own engine. The oracle is bound to the system the
CLI targets, and only enabled when that is the local Docker trial:

  SAP_HOST                 must be local (localhost / 127.0.0.1 / ::1)
  SAP_DOCKER_CONTAINER     the container (default a4h, as bw_activation.py)
  SAP_HANA_SCHEMA          the ABAP schema (default SAPA4H)

It probes once with `SELECT 1 FROM DUMMY`. When that fails (no docker, remote
system, container down) the checks are skipped with the reason. After a
successful probe a failing query FAILS the test: a wrong table name or a
syntax error must not pass as "skipped".
"""

import os
import shutil
import subprocess

import pytest

_LOCAL_HOSTS = {"localhost", "127.0.0.1", "::1"}
_HDBSQL = "/usr/sap/A4H/hdbclient/hdbsql"


class HanaOracle:
    def __init__(self):
        self.container = os.getenv("SAP_DOCKER_CONTAINER", "a4h")
        self.schema = os.getenv("SAP_HANA_SCHEMA", "SAPA4H")
        self._unavailable = None  # reason, once probed and unusable
        self._probed = False

    def table(self, name):
        """`name` qualified with the configured schema, e.g. SAPA4H.D020S."""
        return f"{self.schema}.{name}"

    def _run(self, sql):
        return subprocess.run(
            ["docker", "exec", "-u", "a4hadm", "-e", "HOME=/home/a4hadm", self.container,
             _HDBSQL, "-U", "DEFAULT", "-a", "-x", sql],
            capture_output=True, text=True, timeout=120,
        )

    def _probe(self):
        self._probed = True
        host = os.getenv("SAP_HOST", "localhost")
        if host not in _LOCAL_HOSTS:
            self._unavailable = f"SAP host {host!r} is not the local Docker trial"
        elif shutil.which("docker") is None:
            self._unavailable = "docker not available"
        else:
            proc = self._run("SELECT 1 FROM DUMMY")
            if proc.returncode != 0 or proc.stdout.strip() != "1":
                self._unavailable = (f"HANA in container {self.container!r} not reachable: "
                                     f"{(proc.stderr or proc.stdout)[:200]}")

    def query(self, sql):
        """Rows of a read-only SELECT, each a list of unquoted column strings."""
        if not self._probed:
            self._probe()
        if self._unavailable:
            pytest.skip(f"HANA cross-check unavailable: {self._unavailable}")
        proc = self._run(sql)
        assert proc.returncode == 0, f"HANA query failed: {sql}\n{proc.stderr or proc.stdout}"
        return [[c.strip('"') for c in line.split(",")]
                for line in proc.stdout.strip().splitlines() if line]


_ORACLE = None


def oracle():
    """The session's HanaOracle (probed on first use)."""
    global _ORACLE
    if _ORACLE is None:
        _ORACLE = HanaOracle()
    return _ORACLE
