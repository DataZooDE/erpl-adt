"""Bring the BW Modeling API up on the local Docker ABAP trial.

The BW Modeling REST API and BW Search are not active on a freshly started
`sapse/abap-cloud-developer-trial` container, and the activation does not
survive a restart — so every fresh container silently turned the BW suites into
skips. This module probes for BW and, if it is down, switches it on.

Activation is not a REST call: it writes `ICFACTIVE`/`ACTIVEFL` directly in
HANA and restarts the SAP instance. That is a destructive, infrastructure-level
change, so it is confined to a system we can positively identify as the local
throwaway trial:

* `SAP_BW_AUTOACTIVATE=auto` (default) — activate only when the SAP host is
  local *and* the configured Docker container is running.
* `SAP_BW_AUTOACTIVATE=never` — never activate; BW suites skip as before.
* `SAP_BW_AUTOACTIVATE=always` — activate regardless of those checks. Only for
  a disposable system you own; it restarts the instance.

Anything else — a remote host, no Docker, a container under another name —
falls through to "BW unavailable" and the suites skip, exactly as they did
before. Pointing the suite at a real BW system can never trigger this.
"""

import os
import shutil
import subprocess
import time

# ICF node GUIDs. Delivered content, stable across restarts of the same image.
_BW_NODE_GUID = "DFFAEATGKMFLCDXQ04F0J7FXK"        # parent of /sap/bw/
_MODELING_NODE_GUID = "3FWVDBADCM6B4KLQKF4R70SS5"  # parent of /sap/bw/modeling/

_HDBSQL = "/usr/sap/A4H/hdbclient/hdbsql"
_LOCAL_HOSTS = {"localhost", "127.0.0.1", "::1"}

# A full instance restart, then the ABAP work processes coming up behind it.
_RESTART_TIMEOUT_SEC = 900
_PROBE_INTERVAL_SEC = 15


def _container():
    return os.getenv("SAP_DOCKER_CONTAINER", "a4h")


def _policy():
    return os.getenv("SAP_BW_AUTOACTIVATE", "auto").strip().lower()


def _hana_credentials():
    user = os.getenv("SAP_HANA_USER", "SAPA4H")
    # The trial ships with one password for both; allow them to diverge.
    password = os.getenv("SAP_HANA_PASSWORD") or os.getenv("SAP_PASSWORD", "")
    return user, password


def _run(args, timeout):
    """Run a command, returning (ok, combined_output)."""
    try:
        proc = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except (subprocess.TimeoutExpired, OSError) as exc:
        return False, str(exc)
    return proc.returncode == 0, (proc.stdout or "") + (proc.stderr or "")


def _hdbsql(sql, timeout=120):
    user, password = _hana_credentials()
    return _run(
        ["docker", "exec", _container(), _HDBSQL, "-i", "02", "-d", "HDB",
         "-u", user, "-p", password, sql],
        timeout=timeout,
    )


def _cli_binary():
    return os.getenv(
        "ERPL_ADT_BINARY",
        os.path.join(os.path.dirname(__file__), "..", "..", "build", "erpl-adt"),
    )


def _cli(args, host, port, user, password, client, timeout):
    """Run erpl-adt against the system. Returns (ok, output)."""
    binary = _cli_binary()
    if not os.path.isfile(binary):
        return False, f"binary not found: {binary}"
    return _run(
        [binary, "--host", str(host), "--port", str(port), "--user", user,
         "--password", password, "--client", client, "--json=true", *args],
        timeout=timeout,
    )


def bw_is_reachable(host, port, user, password, client, timeout=60):
    """True if the BW Modeling API answers.

    Probes with erpl-adt itself rather than a hand-rolled HTTP request: the
    point is whether *the client under test* can reach BW, and a bespoke probe
    that disagrees with it is worse than no probe — a false negative here
    restarts the SAP instance for nothing.
    """
    ok, _ = _cli(["bw", "discover"], host, port, user, password, client, timeout)
    return ok


def _container_is_running():
    if shutil.which("docker") is None:
        return False
    ok, out = _run(
        ["docker", "inspect", "-f", "{{.State.Running}}", _container()],
        timeout=30)
    return ok and out.strip() == "true"


def _may_activate(host):
    """Whether we are allowed to switch BW on for this system."""
    policy = _policy()
    if policy == "never":
        return False, "SAP_BW_AUTOACTIVATE=never"
    if policy == "always":
        return True, "SAP_BW_AUTOACTIVATE=always"
    if host not in _LOCAL_HOSTS:
        return False, f"SAP host {host!r} is not local — refusing to modify it"
    if not _container_is_running():
        return False, f"Docker container {_container()!r} is not running"
    return True, f"local Docker trial ({_container()})"


def _switch_on_icf_nodes():
    """Flip the ICF nodes and BW Search on. Idempotent."""
    statements = [
        ("BW ICF node",
         "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'BW' "
         f"AND ICFPARGUID = '{_BW_NODE_GUID}'"),
        ("MODELING ICF node",
         "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'MODELING' "
         f"AND ICFPARGUID = '{_MODELING_NODE_GUID}'"),
        ("BW Search",
         "UPDATE SAPA4H.RSOSSEARCH SET ACTIVEFL = 'X' WHERE TLOGO = 'BIMO'"),
    ]
    for label, sql in statements:
        ok, out = _hdbsql(sql)
        if not ok:
            return False, f"{label}: {out.strip()}"
    return True, ""


def _restart_instance():
    """Restart the SAP instance — SIGHUP does not flush the ICF service cache."""
    ok, out = _run(
        ["docker", "exec", _container(), "bash", "-c",
         "su - a4hadm -c 'sapcontrol -nr 00 -function RestartInstance'"],
        timeout=300)
    if not ok:
        return False, out.strip()
    ok, out = _run(
        ["docker", "exec", _container(), "bash", "-c",
         "su - a4hadm -c 'sapcontrol -nr 00 -function WaitforStarted 600 10'"],
        timeout=_RESTART_TIMEOUT_SEC)
    return ok, out.strip()


def _wait_for_adt(host, port, user, password, client, deadline):
    """Wait for ADT to answer again after the restart."""
    while time.time() < deadline:
        ok, _ = _cli(["discover", "services"], host, port, user, password,
                     client, timeout=60)
        if ok:
            return True
        time.sleep(_PROBE_INTERVAL_SEC)
    return False


def ensure_bw_activated(host, port, user, password, client, log=print):
    """Probe BW; switch it on when it is down and we are allowed to.

    Returns True if BW is reachable afterwards. Never raises — a system we
    cannot or may not activate simply reports False, and the BW suites skip.
    """
    if bw_is_reachable(host, port, user, password, client):
        return True

    allowed, reason = _may_activate(host)
    if not allowed:
        log(f"BW Modeling API is not active and will not be activated: {reason}")
        return False

    log(f"BW Modeling API is not active — activating on {reason}. "
        "This restarts the SAP instance and takes several minutes.")

    ok, detail = _switch_on_icf_nodes()
    if not ok:
        log(f"BW activation failed while updating HANA tables: {detail}")
        return False

    ok, detail = _restart_instance()
    if not ok:
        log(f"BW activation failed while restarting the instance: {detail}")
        return False

    deadline = time.time() + _RESTART_TIMEOUT_SEC
    if not _wait_for_adt(host, port, user, password, client, deadline):
        log("SAP did not answer on ADT again after the restart")
        return False

    # The ICF cache is warm again, but BW itself may need a moment more.
    while time.time() < deadline:
        if bw_is_reachable(host, port, user, password, client):
            log("BW Modeling API is active")
            return True
        time.sleep(_PROBE_INTERVAL_SEC)

    log("BW Modeling API still unreachable after activation")
    return False
