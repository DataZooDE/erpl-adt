---
name: erpl-adt-catalog
description: >
  Building a searchable metadata catalog of an SAP system with erpl-adt catalog: catalog build --sid --package
  --infoarea (--format table|json|openmetadata|mermaid, --db to persist into DuckDB, --embed for vector search),
  incremental catalog sync with --resume, business annotations with catalog annotate (--file overlay.yaml or
  --id with --definition/--owner/--lob/--confidentiality), full-text, vector or hybrid catalog search, and the
  catalog webui (--host, --auth-token-env). Use when someone wants an inventory or data catalog of ABAP/DDIC/CDS
  packages and BW infoareas, to search it, curate business definitions, or export it to OpenMetadata.
user-invocable: false
---

# erpl-adt — Metadata Catalog Cheat Sheet

## Critical gotchas

1. **`--sid` is required** on build and sync — it stamps every entity with the system id.
2. **Scope first.** Find the packages with `erpl-adt --json search 'Z*' --type DEVC` before building; a whole
   system is a long run.
3. **`--embed` sends entity text to the Gemini API** (needs `GEMINI_API_KEY`). Leave it off for confidential
   systems; full-text search works without it.
4. **The web UI binds 127.0.0.1**; binding elsewhere needs `--auth-token-env`.

## Build, sync, search

```bash
erpl-adt catalog build --sid A4H --package ZTEST,ZERPL --infoarea 0D_NW_DEMO --db catalog.duckdb
erpl-adt catalog sync catalog.duckdb --sid A4H --package ZTEST --resume
erpl-adt --json catalog search catalog.duckdb 'flight booking' --mode fts --max 20
erpl-adt catalog build --sid A4H --package ZTEST --format openmetadata > catalog.json
```

The catalog is a DuckDB file — query it directly with DuckDB as well.

## Curate

```bash
erpl-adt catalog annotate catalog.duckdb --id <entity_id> --definition 'Booked seats per flight' --lob Operations
erpl-adt catalog annotate catalog.duckdb --file overlay.yaml
```

## Browse

```bash
erpl-adt catalog webui catalog.duckdb --port 9000
```

See also: `erpl-adt-bw` (BW exports), `erpl-adt-setup`. For SAP *data* in DuckDB, the erpl plugin.
