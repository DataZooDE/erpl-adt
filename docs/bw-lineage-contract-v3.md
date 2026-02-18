# BW Lineage Truth Contract v3

This document defines the protocol-first upstream lineage contract for `bw read-query`.

## Resolution Rules

1. Candidate DTP discovery uses `bwSearch` with `dependsOnObjectName` + `dependsOnObjectType` when available.
2. If typed dependency search is empty, fallback to name-only dependency search.
3. `bwModel:feedIncomplete="true"` is treated as incomplete evidence. The planner retries with larger `maxSize` until complete or `--lineage-max-steps` is reached.
4. Candidate DTPs are structurally validated with `read-dtp`:
   - `target.objectName` must equal query `infoProvider`.
   - `target.objectType` must match query `infoProviderType` when present.
5. Ambiguity is explicit:
   - one candidate: auto-selected
   - zero candidates: unresolved
   - more than one candidate: ambiguous
6. Strict mode (`--lineage-strict`) fails when lineage resolution is ambiguous, incomplete, or unresolved.

## JSON Schemas

- `--json-shape=legacy`: backward-compatible graph payload.
- `--json-shape=catalog`: flat ingest-oriented payload for catalog tooling.
- `--json-shape=truth`: contract v3 payload (`contract=bw.query.lineage.truth`) with:
  - `resolution` (mode, selected_dtp, ambiguous, complete, steps, warnings)
  - `candidate_roots`
  - `ambiguities`
  - `nodes`, `edges`, `provenance`, `warnings`

## Evidence Semantics

- `bwSearch.depends_on_typed`: candidate found via typed dependency filter.
- `bwSearch.depends_on_name`: candidate found via name-only dependency filter.
- Edge evidence in truth graph is carried from `edge.attributes`.
