---
name: erpl-adt-bw
description: >
  Working with SAP BW/4HANA models from the shell with erpl-adt bw: searching and reading objects (bw search,
  bw read, read-adso, read-trfn, read-dtp, read-rsds, read-query, nodes, xref), lineage and exports
  (bw lineage, export-query / export-cube / export-area with --mermaid or --shape openmetadata, --out-dir), the
  change cycle (bw lock, bw save, bw validate, bw activate, bw unlock, bw transport), and system information
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
2. **Changes need a lock handle.** `bw lock` returns it; pass it to `bw save`/`bw activate`/`bw delete` with
   `--lock-handle`, and always `bw unlock` afterwards — also after a failure.
3. **Validate before activating**: `bw activate <type> <name> --validate` runs the checks only.
4. **Exports can be large** on a real system; scope them (`export-query`, `export-cube`) or skip parts
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
erpl-adt --json bw lock ADSO ZMYADSO                          # → lock handle
erpl-adt --json bw save ADSO ZMYADSO --lock-handle <handle> --file zmyadso.xml   # payload: see `bw save --help`
erpl-adt --json bw activate ADSO ZMYADSO --validate --lock-handle <handle>
erpl-adt --json bw activate ADSO ZMYADSO --lock-handle <handle> --transport A4HK900001
erpl-adt --json bw unlock ADSO ZMYADSO
```

Check `erpl-adt bw <action> --help` for the exact flags of an action before scripting it.

Querying BW *data* or its repository metadata from SQL (queries, lineage tables) is the erpl plugin
(`erpl-bics-queries`, `erpl-bics-metadata`).

See also: `erpl-adt-catalog`, `erpl-adt-transport`.
