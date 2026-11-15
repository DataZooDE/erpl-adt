---
name: erpl-adt-bw
description: >
  Working with SAP BW/4HANA models from the shell with erpl-adt bw: searching and reading objects (bw search,
  bw read, read-adso, read-trfn, read-dtp, read-rsds, read-query, nodes, xref), lineage and exports
  (bw lineage, export-query / export-cube / export-area with --mermaid or --shape openmetadata, --out-dir), the
  change cycle in one --session-file (bw lock, bw read --version m --raw, bw save, bw activate --validate,
  bw activate, bw unlock), and system information
  (sysinfo, dbinfo, applog, datavolumes). Use when documenting, diagramming, changing or activating BW objects
  (aDSO, transformations, DTPs, queries, CompositeProviders) outside the BW modeling tools.
user-invocable: false
---

# erpl-adt — BW Modeling Cheat Sheet

`erpl-adt bw …` uses the BW modeling REST API. Reading is safe; changing follows lock → save → validate →
activate → unlock, with a transport where the object is transportable.

## Critical gotchas

1. **The BW modeling services must be active** on the system (`erpl-adt --json bw discover`). On the ABAP
   Platform Trial they are off until set up.
2. **One session for the whole change.** `bw lock` returns a lock handle (and a timestamp) that is valid only
   in its session: run lock, save, activate and unlock with the same `--session-file "$SESSION"` (a global
   flag), pass `--lock-handle`, and always `bw unlock` afterwards — also after a failure. The session file holds
   cookies and CSRF tokens: keep it in a private temp directory, never in the repository, never share it.
3. **Validate before activating**: `bw activate <type> <name> --validate` runs the model checks only.
   (`bw validate` is a name/existence check, not a model check.)
4. **`bw delete` deletes immediately** — a lock handle is not consent. Confirm system, type, name and transport
   with the user first.
5. **Exports can be large** on a real system; scope them (`export-query`, `export-cube`) or skip parts
   (`--no-queries`, `--no-lineage`).

## Read

```bash
erpl-adt --json bw search 'ZERPL*'
erpl-adt --json bw read ADSO ZERPLBIG
erpl-adt --json bw read-adso ZERPLBIG
erpl-adt --json bw read-query 0D_FC_NW_C01_Q0007
erpl-adt --json bw xref ADSO ZERPLBIG
erpl-adt --json bw sysinfo
```

## Lineage and diagrams

```bash
erpl-adt bw export-query 0D_FC_NW_C01_Q0007 --mermaid > query_flow.mmd
erpl-adt bw export-cube 0D_NW_C01 --out-dir ./bw_docs          # {name}_catalog.json + {name}_dataflow.mmd
erpl-adt bw export-area 0D_NW_DEMO --shape openmetadata --out-dir ./bw_docs
erpl-adt --json bw lineage <DTP_NAME>
```

## Change cycle

```bash
SESSION="$(mktemp -d)/bw-session.json"
erpl-adt --json --session-file "$SESSION" bw lock ADSO ZMYADSO          # → lock handle, timestamp
erpl-adt --session-file "$SESSION" bw read ADSO ZMYADSO --version m --raw > zmyadso.xml   # payload to edit
# … edit zmyadso.xml …
erpl-adt --json --session-file "$SESSION" bw save ADSO ZMYADSO --lock-handle <handle> --file zmyadso.xml
erpl-adt --json --session-file "$SESSION" bw activate ADSO ZMYADSO --validate --lock-handle <handle>
erpl-adt --json --session-file "$SESSION" bw activate ADSO ZMYADSO --lock-handle <handle> --transport A4HK900001
erpl-adt --json --session-file "$SESSION" bw unlock ADSO ZMYADSO
rm -f "$SESSION"
```

`bw save` also takes `--timestamp` (from the lock response).

Check `erpl-adt bw <action> --help` for the exact flags of an action before scripting it.

Querying BW *data* or its repository metadata from SQL (queries, lineage tables) is the erpl plugin
(`erpl-bics-queries`, `erpl-bics-metadata`).

See also: `erpl-adt-catalog`, `erpl-adt-transport`.
