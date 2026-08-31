# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git Commits and Pull Requests

Do NOT include `Co-Authored-By`, `🤖 Generated with Claude Code`, or any AI attribution lines in commit messages or pull request bodies.

## Project Overview

`erpl-adt` is a CLI and MCP server for the SAP ADT REST API — a single C++ binary that talks the same HTTP endpoints Eclipse ADT uses. It enables AI coding agents and human developers to search, read/write source code, run tests, manage transports, and more against ABAP systems. No Eclipse, no SAP NW RFC SDK, no JVM.

Part of the Datazoo ERPL family. Shares build conventions with flapi and library choices with erpl-web.

## Build Commands

```bash
make release          # Full release build (CMake + Ninja + vcpkg)
make test             # Run unit tests (Catch2, no SAP system needed)
make clean            # Remove the build directory
```

For faster rebuilds during development:
```bash
cmake --build build --target erpl_adt_tests   # Rebuild tests only
ctest --test-dir build --output-on-failure     # Run tests only
```

Build requires: CMake, Ninja, vcpkg (git submodule at `vcpkg/`). Checkout with `--recurse-submodules`.

## Architecture

```
main.cpp -> command_router -> {group handlers}
                                    |
         +---------+---------+------+------+---------+---------+
         v         v         v      v      v         v         v
      search    object    source  test   check   transport   mcp
      locking    ddic    packages abapgit activation  deploy_workflow
         |         |         |      |      |         |         |
         +---------+---------+------+------+---------+---------+
                                    v
                             i_adt_session <-- adt_session (cpp-httplib)
                                    |
                             i_xml_codec  <-- xml_codec   (tinyxml2)
```

All arrows point downward. No cycles. Every horizontal boundary is a pure abstract interface (abstract base class).

**Directory decomposition:** `include/erpl_adt/{core,adt,cli,mcp,config,workflow}` mirrors `src/`. This reflects compilation firewall boundaries — changes to `adt/` internals don't force recompilation of `config/`.

**Key components:**
- `core/types.hpp` — 11 strong types: `PackageName`, `RepoUrl`, `BranchRef`, `RepoKey`, `SapClient`, `ObjectUri`, `ObjectType`, `TransportId`, `LockHandle`, `CheckVariant`, `SapLanguage`
- `core/result.hpp` — `Result<T,E>` discriminated union + `Error` struct with `ErrorCategory` for exit codes
- `adt/i_adt_session.hpp` — Abstract HTTP session (`Get`, `Post`, `Put`, `Delete`, stateful sessions)
- `adt/i_xml_codec.hpp` — Abstract XML codec (legacy, used only by deploy workflow)
- `adt/{search,object,locking,source,testing,checks,transport,ddic}.hpp` — Operation modules, stateless free functions taking `IAdtSession&`
- `adt/{packages,abapgit,activation}.hpp` — Deploy/bootstrap operations (`packages` uses `IXmlCodec`; `abapgit` and `activation` use shared async protocol contracts)
- `cli/command_router.hpp` — Two-level dispatch: `erpl-adt <group> <action> [args]`
- `cli/output_formatter.hpp` — Human-readable table and JSON output
- `mcp/mcp_server.hpp` — JSON-RPC 2.0 server over stdio; negotiates MCP 2025-06-18 / 2025-03-26 / 2024-11-05
- `mcp/tool_registry.hpp` — Tool name → handler mapping, plus titles, annotations and output schemas
- `mcp/tool_metadata.hpp` — The reviewed read-only/destructive classification for every tool
- `mcp/http_security.hpp` — Origin allowlisting and optional bearer auth for the HTTP transport
- `config/config_loader.hpp` — Merges CLI args (argparse) + YAML (yaml-cpp) into `AppConfig`
- `workflow/deploy_workflow.hpp` — Idempotent state machine: discover → package → clone → pull → activate
- `workflow/lock_workflow.hpp` — Lock transaction orchestration for CLI auto-lock flows
- `src/adt/protocol_kernel.hpp` — Shared 202+Location async polling contract
- `src/adt/{xml_utils,atom_parser}.hpp` — Shared parser primitives for namespaced XML/Atom feeds

**XML parsing strategy:** Operation modules parse XML with tinyxml2 and shared parser helpers (`xml_utils`, `atom_parser`) to reduce duplicated namespaced traversal logic. `IXmlCodec` is preserved for legacy deploy workflow paths.

**Testing:** Hand-written mocks in `test/mocks/` implementing the abstract interfaces. No mocking framework. Test fixtures in `test/testdata/` are real captured Eclipse ADT XML traffic.

## Design Constraints

These are hard requirements, not suggestions:

- **C++17.** `-Werror` enabled — treat all warnings as errors.
- **No raw `new`/`delete`** — `unique_ptr`/`shared_ptr` only. No global mutable state.
- **No exceptions for expected failures.** Use `Result<T,E>`. Exceptions only for programming errors.
- **Strong types** for all domain concepts. Private constructors + `Create()` factory returning `Result<T, string>`. No sentinel values — use `std::optional`.
- **Constructor injection** for all dependencies. Every component testable in isolation via mock collaborators.
- **RAII everywhere** — HTTP sessions, CSRF tokens, lock handles (`LockGuard`).
- **Const-correctness** — all non-mutating methods `const`, all non-modified params `const&` or `string_view`.
- **No implicit conversions, no C-style casts, no `void*`.**
- **Public interfaces minimal.** Internal helpers in anonymous namespaces, never in headers.

## Common Pitfalls

- `Result<T,E>` name collides with member methods named `Result()` — rename to avoid ambiguity (e.g., `LockInfo()`)
- Strong types have no default constructors — aggregate init of structs containing them must use brace init with values
- `Error` struct's `category` field is `ErrorCategory` (NOT optional)
- Use explicit field assignment for multi-field structs, not aggregate init (triggers `-Wmissing-field-initializers`)
- `[[nodiscard]]` return values must be captured in tests

## ABAP Cloud Rules (important for agent-written ABAP code)

- **No MANDT in WHERE clauses:** `DELETE FROM table WHERE mandt = sy-mandt` → error "client field cannot be in WHERE condition". Use `DELETE FROM table.` — client filtering is automatic.
- **TABL/DT (transparent table) CDS source** requires four annotations before `define table`:
  ```abap
  @AbapCatalog.enhancement.category : #NOT_EXTENSIBLE
  @AbapCatalog.tableCategory : #TRANSPARENT
  @AbapCatalog.deliveryClass : #A
  @AbapCatalog.dataMaintenance : #RESTRICTED
  ```
  Missing any annotation → SAP returns HTTP 400 "Can't save due to errors in source; execute check for details".

## Dependencies (vcpkg)

| Port | Role |
|------|------|
| cpp-httplib[openssl] | HTTP/HTTPS client |
| tinyxml2 | XML parse + build |
| yaml-cpp | Config file parsing |
| argparse | CLI argument parsing |
| nlohmann-json | JSON for MCP protocol + CLI output |
| openssl | TLS |

Test dependency: Catch2.

## ADT Protocol Reference

CSRF: every mutating request needs `x-csrf-token`. Fetch via GET with `x-csrf-token: fetch` header. On 403 → re-fetch and retry once.

Async ops (pull, activation): return `202 Accepted` + `Location` header. Poll until `completed` or `failed`.

Stateful sessions: `X-sap-adt-sessiontype: stateful` header + `sap-contextid` cookie for locking/write operations. `LockGuard` RAII class manages the lifecycle.

Logon language: the connection language is chosen with the global `--language <iso>` flag (2-letter ISO, e.g. `EN`, `DE`; default `EN`). It is sent as the `Accept-Language` header (lower-cased), which SAP maps to the logon language — language-dependent text (object descriptions) then comes back translated. Threaded via `AdtSessionOptions::language` (a `std::optional<SapLanguage>`); also settable in YAML config (`connection.language`) and persisted in `.adt.creds`.

## Catalog Web UI

`flutter/erpl_catalog_kit` is a Flutter web client for the `catalog_*` MCP tools (search, browse, lineage, curate, sync status, feed export). Its compiled web build can be embedded directly into the `erpl-adt` binary via CMakeRC (vendored as the `third_party/cmrc` submodule) and served same-origin with the JSON-RPC catalog API — no separate static file server, no CORS setup.

```bash
make webui              # flutter build web (requires the Flutter SDK) — run once, or after any client change
make release             # picks up flutter/erpl_catalog_kit/example/build/web if present, embeds it
./build/erpl-adt catalog webui catalog.duckdb --port 8383   # blocks; open http://127.0.0.1:8383/
```

`make webui` is optional — C++-only contributors don't need the Flutter SDK. If `flutter/erpl_catalog_kit/example/build/web/index.html` doesn't exist at CMake configure time, `ERPL_ADT_HAVE_WEBUI` stays undefined and `erpl-adt catalog webui` still builds and runs, but serves a plain-text instructional message (`run 'make webui' ...`) instead of the app. `ERPL_ADT_HAVE_WEBUI` is a PUBLIC compile definition on `erpl_adt_lib` (unlike `ERPL_ADT_TELEMETRY_ENABLED`) so `test/mcp/test_mcp_http_server.cpp` can assert on whichever behavior the current build actually has.

`McpHttpServer`'s `serve_webui` constructor parameter (default `false`) gates a catch-all `Get(".*")` route registered after `/mcp` and `/healthz`. It resolves the embedded `cmrc::embedded_filesystem` and falls back to `index.html` for any unmatched path — required because go_router does client-side routing (e.g. `/entity/<id>`), so a hard refresh or deep link must resolve client-side rather than 404.

Both `.github/workflows/release.yaml` and `.github/workflows/build.yaml` run `make webui`'s equivalent (Flutter SDK setup + `flutter build web`) before configuring CMake, on all 4 platforms (linux-x86_64, macos-arm64, macos-x86_64, windows-x64) — so every released binary, and every CI build on every push/PR, embeds the web UI. `scripts/ci/webui_smoke.py` then starts `catalog webui` against a scratch DuckDB file and asserts the real app is served (`/` returns 200 HTML, `/main.dart.js` loads, the SPA deep-link fallback works), catching a silent embed failure before it ships — on every platform, not just one.

## CLI Verbosity

- Default: warnings only (quiet)
- `-v`: INFO-level logging (HTTP method + URL + status code)
- `-vv`: DEBUG-level logging (request/response headers, cookies, CSRF tokens)
- Logging uses `core/log.hpp` global logger (`LogInfo`, `LogDebug`, etc.) writing to stderr

Package existence (release-dependent): `/sap/bc/adt/packages/{name}` does not exist on
SAP_BASIS 7.40 — it answers 404 for *every* package there, so a 404 from it proves
nothing. `ResolvePackageExistence()` falls back to the repository information system
search (`objectType=DEVC/K`, exact name match), which is available on every release, and
reports which oracle answered via `resolved_via`. Never map that 404 straight to "does
not exist". Related: `nodestructure` answers HTTP 200 with an *empty body* for both
"package is empty" and "package does not exist", so it cannot serve as the oracle either
— those two states must be distinguished separately.

BW content negotiation (every `/sap/bw/modeling/` route): a request that does not name
the object type's media type in `Accept` is answered with HTTP 415 ("Requested content
type */* does not match back-end content type ..."). cpp-httplib sends no `Accept` of its
own, so every BW call must set one explicitly — reads *and* mutations. The catalog lives
in `adt/bw_media_types.hpp` (`BwDefaultMediaType`), overridden by the discovery-resolved
type.

BW create (`POST /sap/bw/modeling/{tlogo}/{name}`) always parses a request body: an empty
one answers HTTP 500 "Object MODEL <name> not found", and the `copyFrom*` query parameters
do not fill it in — a copy is done by reading the source definition and POSTing it under
the new name. The target package travels in the body as `<adtcore:packageRef>`, not as a
`package` query parameter (which the discovery template does not advertise). ADSO bodies
need `schemaVersion=""` or the backend answers 500 "Attribute 'schemaVersion' expected".
Created objects exist only in the inactive (`m`) version until activated.

BW activation (`POST /sap/bw/modeling/activation`, or `/checkruns` to check without
activating) takes an **Atom feed with exactly one entry** — `CL_RSO_RES_ACTIVATION`
deserializes the body with `cl_atom_feed_prov->get_feed()`. The entry's `rel="self"` link
names the object; its `<atom:content>` must hold a `bwModel:checkProperties` element
(namespace `http://www.sap.com/bw/modeling`) whose `version`, `modelContent` and
`lockHandle` attributes are mandatory — `RSO_RES_ST_BW_CHECKRUN` is the transformation
that maps them. The mode comes from the *path*, not a query parameter, and there is no
async variant. The **type segment of the object URI must be lower case**:
`/sap/bw/modeling/ADSO/...` makes the backend dump with HTTP 500, `/adso/...` works.
Anything else answers HTTP 500 "Request cannot be deserialized". The response is an Atom
feed of check messages: severity in `bwModel:checkresult/@messageType`, text in the
entry's `<atom:title>`.

Reading the backend is the fastest way to settle a payload question: the ABAP source is
right there over ADT. `erpl-adt search 'CL_RSO_RES*'` finds the resource controllers, and
`GET /sap/bc/adt/oo/classes/<name>/source/main` (plus `/includes/implementations` for the
local classes) shows exactly what the deserializer expects — that is how the shape above
was established rather than guessed.

The create *verb* depends on the object type. ADSO creates with POST; the InfoObject
resource controller (`CL_RSO_RES_INFO_OBJECT`) implements only `get()`, so POSTing a name
that does not exist yet answers HTTP 404 "Resource IOBJ &lt;name&gt; does not exist" and PUT is
the create verb there — while PUT on a *new* ADSO answers 400 "Parameter version could not
be found". `BwCreateObject` therefore tries POST and retries with PUT on a 404, which reads
the answer off the system instead of hard-coding a verb for each of the 40-odd types.

Renaming a copied object must match whole names only: copying `0CALMONTH` with a plain
substring replace also renamed the referenced `0CALMONTH2`, and the copy then failed
activation with "Attribute ...2 not (actively) available".

Saving (`PUT`) addresses the **version segment**: `/sap/bw/modeling/{tlogo}/{name}/m`.
Without it the backend answers HTTP 400 "Parameter version could not be found", and
`?version=M` does not satisfy it either — which is why `bw save` had never worked.

Auditing the BW surface is mechanical: list the calls, then probe each against a
*non-existent* object so nothing can be mutated, and read what comes back. HTTP 405
"Resource controller does not support method X" means the wrong verb; 415 names the type
the route serves; 400 names the missing parameter. That sweep found `bw validate` (GET on
a POST-only route, and `action=validate` is not one of the accepted actions — they are
exists / new / standard_transport / is_plannable), `bw qprops` (serves
`infoprov_query_props`, not the `rulesQueryProperties` discovery advertises, and needs an
`infoprovider`), `bw applog` (username, starttimestamp and endtimestamp are all mandatory)
and `bw move` (the endpoint executes moves and never had a listing). A clean `bw validate`
answers 200 with an *empty body* — that is the success case, not a malformed response.

Several ADT endpoints answer HTTP 200 for a target that is not there, and the answer reads
like success: `classrun` returns "Object X of type CLAS does not exist." as its *console
output*, an ATC run returns an empty finding list (indistinguishable from "clean"), a test
run returns `all_passed: true` with "no test methods", and a transport release accepts the
job without validating the number. SAP's wording is translated, so matching on it is
fragile — `EnsureObjectExists` (adt/object_exists.hpp) does a plain GET first, and every
one of those commands calls it before acting.

Not verifiable on the a4h trial: the **abapGit ADT backend is not installed** —
`/sap/bc/adt/abapgit` answers 404 and abapgit appears nowhere in the ADT discovery
document. So `abapgit.cpp` and the `deploy` workflow that drives it have never been
measured against a live system, and the audit that swept BW, the ADT read/write surface
and the MCP tools does not cover them. Treat their request shapes as unconfirmed rather
than as working.

BW discovery advertises several templates per type. The *first* one is `rel="self"` and
has no `{version}` segment; the versioned route is `rel="latest-version"`. Resolve by
relation (`BwResolveEndpointByRel`) — taking the first match silently drops a requested
version.

Key endpoints:
- `/sap/bc/adt/discovery` — service discovery
- `/sap/bc/adt/repository/informationsystem/search` — object search
- `/sap/bc/adt/oo/classes/{name}` — class CRUD
- `/sap/bc/adt/oo/classes/{name}/source/main` — source read/write
- `/sap/bc/adt/abapunit/testruns` — ABAP Unit testing
- `/sap/bc/adt/atc/worklists` — ATC quality checks
- `/sap/bc/adt/cts/transportrequests` — transport management
- `/sap/bc/adt/repository/nodestructure` — package contents
- `/sap/bc/adt/ddic/tables/{name}` — table definitions
- `/sap/bc/adt/abapgit/repos` — abapGit operations

Protocol is undocumented. Ground truth comes from captured Eclipse ADT traffic in `test/testdata/`. Reference implementations: `abapGit/ADT_Backend` and `marcellourbani/abap-adt-api` on GitHub.

## Test Strategy

**Unit tests:** ~387 tests using Catch2. All run offline with mock sessions — no SAP infrastructure needed.

**Integration tests:** Python/pytest in `test/integration_py/`. Run against a live SAP ABAP Cloud Developer Trial (Docker). Test the actual ADT REST API endpoints. Every test logs the exact CLI command invoked, making the test suite executable CLI documentation.

**Execution cadence (required for refactoring work):**
- Run integration tests after each completed task, not only at the end of an epic.
- Minimum cadence: run `make test-integration-py-smoke` after each task; run full `make test-integration-py` for task DoD and before closing the related GitHub issue.
- If the live SAP system is unavailable, record the connectivity blocker immediately and rerun as soon as connectivity is restored.

```bash
make test                          # Unit tests only (offline, fast)
make test-integration-py           # Python integration tests (requires SAP system)
make test-integration-py-smoke     # Smoke subset only (health + discovery)
```

Integration tests require `SAP_PASSWORD` env var. Defaults: localhost:50000, DEVELOPER, client 001.

**Acceptance criteria:** Integration tests are complete when `SAP_PASSWORD=... uv run pytest -v` in `test/integration_py/` passes all tests against a real SAP system.

Exit codes: 0=success, 1=connection/auth/authorization, 2=package/notfound, 3=clone, 4=pull, 5=activation, 6=lock conflict, 7=test failure, 8=ATC check error, 9=transport error, 10=timeout, 99=internal.

HTTP 403 is classified by evidence, not by status alone: a 403 carrying a SAP
application error is not a CSRF problem — "currently editing"/"locked by" is a
`lock_conflict` (exit 6), anything else is `authorization` (exit 1). Only a bare 403
with no SAP payload is treated as `csrf_token`, which is also the shape the session
layer retries once. A 403 raised while *fetching* a token is never `csrf_token` —
there was no token yet.

Test directory structure:
- `test/core/` — types, result
- `test/adt/` — all ADT operation modules (unit tests with mocks)
- `test/cli/` — command router, output formatter, CLI examples
- `test/mcp/` — MCP server, tool registry
- `test/config/` — config loading
- `test/workflow/` — deploy workflow
- `test/mocks/` — hand-written mock implementations
- `test/testdata/` — captured ADT XML traffic
- `test/integration_py/` — Python/pytest integration tests against live SAP system

Source GLOB patterns in `test/CMakeLists.txt` — new C++ test directories need explicit glob entries.

## Python Tooling

**Use `uv` for all Python package management.** All Python commands must be executed via `uv run` to ensure the correct virtual environment is used.

```bash
cd test/integration_py && uv run pytest -v           # Run integration tests
cd test/integration_py && uv run pytest -v -m smoke   # Smoke tests only
cd test/integration_py && uv sync                     # Install/update dependencies
```

Never use bare `python` or `pip` commands — always `uv run`.

## Cross-Platform Targets

| Target | Toolchain | Static linking |
|--------|-----------|---------------|
| Linux x86_64 | GCC 13+ | `-static-libgcc -static-libstdc++`, OpenSSL static |
| macOS arm64 | Apple Clang 15+ | vcpkg default static, system OpenSSL |
| macOS x86_64 | Apple Clang 15+ | Same as arm64 |
| Windows x64 | MSVC 17+ | `/MT` (static CRT), triplet `x64-windows-static` |

## Release Process

Versioning: `v{YYYY}.{MM}.{DD}` date-based tags. Bugfix same-day releases append a suffix (e.g., `v2026.02.14.1`).

1. Ensure all CI builds pass on `main` (`gh run list`)
2. Tag: `git tag v{YYYY}.{MM}.{DD} && git push origin v{YYYY}.{MM}.{DD}`
3. The `.github/workflows/release.yaml` triggers on `v*` tag push:
   - Builds on 4 platforms (linux-x86_64, macos-arm64, macos-x86_64, windows-x64)
   - Runs all unit tests on each platform
   - Validates `--version` output matches tag (linux only)
   - Packages archives (`.tar.gz` for Unix, `.zip` for Windows) with SHA256 checksums
   - Creates GitHub Release via `softprops/action-gh-release@v2` with `generate_release_notes: true`
4. After the workflow completes, edit the release body with a hand-written changelog:
   `gh release edit v{tag} --notes-file release-notes.md`

## BW Modeling API — Activating on the a4h Docker Container

After a container restart, the BW Modeling REST API and BW Search are **not active**.
They must be activated by writing directly to HANA tables, then restarting the SAP instance.

The Python integration suite does this for you: `pytest_sessionstart` probes BW with
`erpl-adt bw discover` and, when it is down, runs the steps below and waits for SAP to
come back (several minutes). Because that writes to HANA system tables and restarts the
instance, it only fires on a system identifiable as the local throwaway trial — SAP host
local *and* the Docker container running. Override with `SAP_BW_AUTOACTIVATE=never`
(skip BW suites instead) or `=always` (disposable systems only), and point it at another
container with `SAP_DOCKER_CONTAINER`. Anything remote is refused outright, so the suite
can never restart a real BW system. See `test/integration_py/bw_activation.py`.

The manual steps remain below, for doing it by hand.

The CLI shows actionable hints when services are missing:
- HTTP 404 on any `/sap/bw/modeling/` path → SICF not activated
- HTTP 403 on any `/sap/bw/modeling/` path (including the CSRF fetch, which is where
  an inactive node usually bites first) → SICF not activated, or missing authorization
- HTTP 500 "not activated" on `/bwsearch` → BW Search not activated

Hints are attached in the session layer too, not only in the BW modules, so failures
raised below them (a rejected CSRF fetch) still carry one.

### Step 1 — Activate /sap/bw/ and /sap/bw/modeling/ in ICFSERVLOC

Use **SAPA4H** (the table owner) — not SYSTEM. Granting to SYSTEM does not work reliably
(`insufficient privilege` at UPDATE time even after GRANT).

```bash
# Activate the /sap/bw/ node (parent GUID constant: DFFAEATGKMFLCDXQ04F0J7FXK)
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'BW' AND ICFPARGUID = 'DFFAEATGKMFLCDXQ04F0J7FXK'"

# Activate the /sap/bw/modeling node (BW node GUID constant: 3FWVDBADCM6B4KLQKF4R70SS5)
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.ICFSERVLOC SET ICFACTIVE = 'X' WHERE ICF_NAME = 'MODELING' AND ICFPARGUID = '3FWVDBADCM6B4KLQKF4R70SS5'"

# Verify both rows show ICFACTIVE = 'X'
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "SELECT ICF_NAME, ICFPARGUID, ICFACTIVE FROM SAPA4H.ICFSERVLOC WHERE ICF_NAME IN ('BW', 'MODELING')"
```

### Step 2 — Activate BW Search (RSOSSEARCH)

```bash
# Activate BW search for BIMO object type (use SAPA4H as owner)
docker exec a4h /usr/sap/A4H/hdbclient/hdbsql -i 02 -d HDB -u SAPA4H -p 'ABAPtr2023#00' \
  "UPDATE SAPA4H.RSOSSEARCH SET ACTIVEFL = 'X' WHERE TLOGO = 'BIMO'"
```

### Step 3 — Restart the SAP instance to flush the ICF service cache

SIGHUP to icman is **not sufficient** — a full instance restart is required.
`sapcontrol ICMRestart` does not exist; use `RestartInstance` instead.

```bash
docker exec a4h bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function RestartInstance'"
docker exec a4h bash -c "su - a4hadm -c 'sapcontrol -nr 00 -function WaitforStarted 300 10'"
```

### Verify

```bash
./build/erpl-adt --host localhost --port 50000 --user DEVELOPER --password 'ABAPtr2023#00' \
    --client 001 bw discover
./build/erpl-adt --host localhost --port 50000 --user DEVELOPER --password 'ABAPtr2023#00' \
    --client 001 bw search '*' --max 5
```

### Notes

- The GUID constants (`DFFAEATGKMFLCDXQ04F0J7FXK`, `3FWVDBADCM6B4KLQKF4R70SS5`) are stable across
  restarts on the same a4h image — they are part of the delivered content, not generated at runtime.
- `ICFSERVLOC` is client-dependent (SAP client 001). If you switch clients, re-check.
- These steps work on the standard `sapse/abap-cloud-developer-trial:2023` image, verified:
  after activation `bw search '*'` returns delivered BW content (`0BCT_CB`, `0BW`, …) and the
  BW integration suites pass against infoareas such as `0BWTCT`. This note previously said
  that image has no BW Modeling API — it does; the ICF nodes just ship inactive.

## Issue Tracking

GitHub Issues (`gh issue list`, `gh issue view <n>`) is the issue tracker for this
project. There is no local issue database.
