# Read-Query Baseline Audit (Live SAP)

Date: 2026-02-17
Target query: `0D_FC_NW_C01_Q0007` (Monthly Sales by Product Group (Dyn. Date))

## Commands used

```bash
./build/erpl-adt --host localhost --port 50000 --user DEVELOPER --password '***' --client 001 --json bw read QUERY 0D_FC_NW_C01_Q0007 --raw
./build/erpl-adt --host localhost --port 50000 --user DEVELOPER --password '***' --client 001 --json bw read-query query 0D_FC_NW_C01_Q0007
```

## Captured artifacts

- Raw XML: `test/testdata/bw/live_query_0D_FC_NW_C01_Q0007.xml`
- Current parsed output: `test/testdata/bw/live_query_0D_FC_NW_C01_Q0007_parsed_current.json`

## Parity matrix (raw XML -> current parsed JSON)

| Raw source | Expected parsed field | Current status |
|---|---|---|
| `Qry:mainComponent@technicalName="0D_FC_NW_C01_Q0007"` | `name` | OK |
| `Qry:mainComponent/Qry:description@value="Monthly Sales by Product Group (Dyn. Date)"` | `description` | MISSING |
| `Qry:mainComponent@providerName="0D_NW_C01"` | `info_provider` | MISSING |
| `Qry:subComponents@xsi:type="Qry:Variable" technicalName="0D_NW_ACTCMON"` | `references[]` variable node(s) | MISSING |
| `Qry:mainComponent` structural members (`filter`, `rows`, `columns`, `free`, `members`) | `references[]` topology/relationship seeds | MISSING |

## Observed gap summary

The current parser in `bw read-query` is not aligned with SAP `Qry:queryResource` XML shape. It keeps namespace attrs but misses primary semantic fields and emits an empty reference set for this live query.

This audit is the baseline for TDD changes in epic `erpl-deploy-4be`.

## Validation after parser fix

After implementing `Qry:queryResource` parsing support:

```bash
./build/erpl-adt ... --json bw read-query query 0D_FC_NW_C01_Q0007
```

Observed key fields:

- `name = 0D_FC_NW_C01_Q0007`
- `description = Monthly Sales by Product Group (Dyn. Date)`
- `info_provider = 0D_NW_C01`
- `references = 13` (including variable `0D_NW_ACTCMON`)

Post-fix snapshot:
- `test/testdata/bw/live_query_0D_FC_NW_C01_Q0007_parsed_after.json`
