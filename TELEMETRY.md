# Telemetry in erpl-adt

`erpl-adt` collects **anonymous, aggregate usage telemetry** to understand which
features are used and where they fail, so we can prioritise fixes and
improvements. It is **on by default** and trivial to turn off.

It uses the shared DataZoo C++ telemetry library
([`DataZooDE/posthog-telemetry`](https://github.com/DataZooDE/posthog-telemetry),
vendored as a git submodule) and follows the shared `telemetry_schema: 2`
envelope — the same one used by `erpl`, `flapi`, and `anofox-statistics`. Data
is ingested by PostHog in the EU (`https://eu.i.posthog.com`).

## How to opt out

Any **one** of these fully disables telemetry for **both** the CLI and the MCP
server — nothing leaves your machine:

| Method | Scope |
|---|---|
| `--no-telemetry` flag | the single invocation |
| `DATAZOO_DISABLE_TELEMETRY=1` (or `true`/`yes`) | cross-product kill-switch |
| `DO_NOT_TRACK=1` | honours <https://consoledonottrack.com/> |
| `ERPL_ADT_NO_TELEMETRY=1` | product-local |

On the first run with telemetry enabled, erpl-adt prints a one-line notice to
stderr pointing here.

## The privacy contract — what we DO and DON'T send

**We never transmit any of the following**, on any code path:

- ABAP object, package, CDS, or table **names**
- **Source code**, search patterns, or query text
- **Transport IDs**
- SAP **hostnames**, clients, or **user names**
- **File paths**
- **ADT error messages** or any response payload

Only values from a **fixed, code-controlled enumeration** and **numeric
buckets / durations** are ever sent. Object types, operations, and outcomes are
mapped to enums *before* they become a property (via `ClassifyObjectType`,
`BucketCount`, and literal enums in `include/erpl_adt/core/telemetry.hpp`); a raw
identifier can never reach a property value. Counts (ATC findings, deploy
objects) are bucketed — never the exact number. The library additionally clamps
every outgoing string to 512 bytes as a backstop.

There is a unit test (`test/core/test_telemetry.cpp`,
`[telemetry][privacy]`) that feeds adversarial identifiers through the capture
path and asserts none of them appear in any emitted property.

## Identifiers

- **`distinct_id`** — the SHA-256 of your OS machine id (MAC-address fallback).
  It is a stable, pseudonymous machine hash — **not** your username or IP — and
  is identical to the id used by the other DataZoo products, so one machine
  correlates across the stack.
- **`deployment` group** — keyed by the same machine hash, for
  deployment-retention analytics.
- **`account` group** — reserved for a future licensed edition
  (`sha256(license_id)`); erpl-adt ships a single OSS edition today and does not
  associate an account.

## What is collected

Every event carries the shared envelope: `product` (`erpl_adt`),
`product_version`, `product_edition` (`oss`), `os`, `arch`, `is_ci`,
`is_container`, `$session_id` (one per process/run), plus `install_kind`
(`cli` for one-shot commands, `server` for the `mcp` server).

### One-shot CLI

| Event | Properties (beyond envelope) |
|---|---|
| `cli_started` | `command` (subcommand), `args_shape` (which flags were present — **never their values**) |
| `feature_used` | see the feature table below |
| `$exception` | `error_class` (enum), `feature` — **never** a message or identifier |

`$session_id` is one UUID per invocation. Events are flushed at exit.

### `mcp` server

| Event | Properties (beyond envelope) |
|---|---|
| `server_started` | `transport` (`stdio`), `tool_count` |
| `feature_used` (`mcp_tool_called`) | `tool` (registered tool name), `outcome` (`ok`/`error`), `duration_ms` |
| `$exception` | `error_class`, `feature` |

`$session_id` is one UUID per server uptime. Events are flushed on clean
shutdown (stdin EOF) and on `SIGTERM`/`SIGINT`. `SetSampling(rate)` is available
for very high tool-call volume (the effective rate is stamped on events).

### `feature_used` catalogue (fixed enum)

| `feature` | Bounded, non-PII properties |
|---|---|
| `search` | `object_type` ∈ {class, program, function, cds, table, package, other}, `duration_ms` |
| `object_read` | `object_type`, `duration_ms` |
| `object_write` | `object_type`, `op` ∈ {create, update, delete}, `duration_ms` |
| `source_read` | `object_type`, `duration_ms` |
| `source_write` | `object_type`, `checked` (bool), `duration_ms` |
| `activate` | `object_type`, `outcome` ∈ {active, errors}, `duration_ms` |
| `abapunit_run` | `outcome` ∈ {pass, fail}, `duration_ms` |
| `atc_run` | `finding_count_bucket` ∈ {0, 1-10, 11-100, 100+}, `duration_ms` |
| `transport_op` | `op` ∈ {create, list, release}, `duration_ms` |
| `ddic_read` | `kind` ∈ {table, cds}, `duration_ms` |
| `package_read` | `duration_ms` |
| `deploy_run` | `outcome` ∈ {ok, error}, `object_count_bucket`, `duration_ms` |
| `mcp_tool_called` | `tool`, `outcome` ∈ {ok, error}, `duration_ms` |

### `$exception` `error_class` values (fixed enum)

`adt_http_error`, `auth_error`, `lock_error`, `activation_error`, `not_found`,
`test_failure`, `atc_error`, `transport_error`, `timeout`, `usage_error`,
`internal_error`. CLI failures are mapped centrally from the process exit code;
no ADT error text is ever included.
