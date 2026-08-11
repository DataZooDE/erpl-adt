# Activating the BW Modeling API on the a4h Docker Container

After a container restart, the BW Modeling REST API (`/sap/bw/modeling/`) and BW Search are
**not active** by default. This causes `bw read-adso`, `bw search`, and related commands to
fail with HTTP 403 or 404 errors.

Follow the three steps below to activate them. You need direct HANA SQL access (the
`SAPA4H` database user) — the activation bypasses the SAP application layer and writes
directly to the ICF service tables.

## Prerequisites

- Docker container named `a4h` is running
- HANA client available at `/usr/sap/A4H/hdbclient/hdbsql` inside the container
- SAPA4H database user with password `ABAPtr2023#00` (standard a4h image)

## Step 1 — Activate `/sap/bw/` and `/sap/bw/modeling/` in ICFSERVLOC

The two ICFSERVLOC rows have stable GUIDs that are part of the delivered content and do not
change across restarts on the same image.

```bash
# Activate the /sap/bw/ node (parent GUID: DFFAEATGKMFLCDXQ04F0J7FXK)
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'BW' AND ICFPARGUID = 'DFFAEATGKMFLCDXQ04F0J7FXK'"

# Activate the /sap/bw/modeling/ node (BW node GUID: 3FWVDBADCM6B4KLQKF4R70SS5)
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'MODELING' AND ICFPARGUID = '3FWVDBADCM6B4KLQKF4R70SS5'"
```

Verify both rows show `ICFACTIVE = 'X'`:

```bash
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "SELECT ICF_NAME, ICFPARGUID, ICFACTIVE FROM SAPA4H.ICFSERVLOC WHERE ICF_NAME IN ('BW', 'MODELING')"
```

Expected output:

```
ICF_NAME,ICFPARGUID,ICFACTIVE
"BW","DFFAEATGKMFLCDXQ04F0J7FXK","X"
"MODELING","3FWVDBADCM6B4KLQKF4R70SS5","X"
```

## Step 2 — Activate BW Search (RSOSSEARCH)

```bash
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.RSOSSEARCH SET ACTIVEFL = 'X' WHERE TLOGO = 'BIMO'"
```

## Step 3 — Restart the SAP instance

A full instance restart is required to flush the ICF service cache. `SIGHUP` to icman and
`sapcontrol ICMRestart` are **not sufficient**.

```bash
docker exec a4h bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function RestartInstance'"
docker exec a4h bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function WaitforStarted 300 15'"
```

Wait until all four processes show `GREEN`:

```bash
docker exec a4h bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function GetProcessList'"
```

Expected output:

```
name        dispstatus  textstatus
disp+work   GREEN       Running
igswd_mt    GREEN       Running
gwrd        GREEN       Running
icman       GREEN       Running
```

## Verify

```bash
erpl-adt --host localhost --port 50000 --user DEVELOPER --password 'ABAPtr2023#00' \
    --client 001 bw discover

erpl-adt --host localhost --port 50000 --user DEVELOPER --password 'ABAPtr2023#00' \
    --client 001 bw search '*' --max 5
```

`bw discover` should return a table of BW Modeling service URIs. If it still fails with

```json
{"category":"authorization","operation":"FetchCsrfToken","http_status":403,
 "hint":"Access denied — activate /sap/bw/modeling/ in transaction SICF ..."}
```

the ICF service cache was not flushed — confirm the instance fully restarted (all four
processes GREEN) and retry. (Before v2026.08.11 this surfaced as a misleading
`csrf_token` / "CSRF token may be invalid" error, which sent you looking at
authentication rather than at the inactive service.)

## Notes

- The GUID constants are stable on the same a4h image across restarts. They are delivered
  content, not generated at runtime.
- `ICFSERVLOC` is client-dependent (SAP client 001). If you switch clients, re-check.
- The steps above were verified on the standard `sapse/abap-cloud-developer-trial:2023`
  image: after activation `bw search '*'` returns delivered BW content (`0BCT_CB`, `0BW`,
  …) and the BW integration suites pass against infoareas such as `0BWTCT`. An earlier
  version of this note claimed that image has no BW Modeling API; that is not the case —
  the services are present but their ICF nodes ship inactive.
- This activation does **not** persist across Docker image recreations — only across
  container restarts (the HANA data volume is preserved).
- The Python integration suite performs these steps automatically when it finds BW down
  (`test/integration_py/bw_activation.py`), so you rarely need to run them by hand. It
  only does so for a system it can identify as the local throwaway trial; set
  `SAP_BW_AUTOACTIVATE=never` to turn that off.
