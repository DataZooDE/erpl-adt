# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Git Commits

Do NOT include `Co-Authored-By` or any AI attribution lines in commit messages.

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
- `mcp/mcp_server.hpp` — JSON-RPC 2.0 server over stdio (MCP 2024-11-05)
- `mcp/tool_registry.hpp` — Tool name → handler mapping
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

## CLI Verbosity

- Default: warnings only (quiet)
- `-v`: INFO-level logging (HTTP method + URL + status code)
- `-vv`: DEBUG-level logging (request/response headers, cookies, CSRF tokens)
- Logging uses `core/log.hpp` global logger (`LogInfo`, `LogDebug`, etc.) writing to stderr

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
- Run integration tests after each completed beads task, not only at the end of an epic.
- Minimum cadence: run `make test-integration-py-smoke` after each task; run full `make test-integration-py` for task DoD and before closing related beads issues.
- If the live SAP system is unavailable, record the connectivity blocker immediately and rerun as soon as connectivity is restored.

```bash
make test                          # Unit tests only (offline, fast)
make test-integration-py           # Python integration tests (requires SAP system)
make test-integration-py-smoke     # Smoke subset only (health + discovery)
```

Integration tests require `SAP_PASSWORD` env var. Defaults: localhost:50000, DEVELOPER, client 001.

**Acceptance criteria:** Integration tests are complete when `SAP_PASSWORD=... uv run pytest -v` in `test/integration_py/` passes all tests against a real SAP system.

Exit codes: 0=success, 1=connection/auth, 2=package/notfound, 3=clone, 4=pull, 5=activation, 6=lock conflict, 7=test failure, 8=ATC check error, 9=transport error, 10=timeout, 99=internal.

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

The CLI shows actionable hints when services are missing:
- HTTP 404 on any `/sap/bw/modeling/` path → SICF not activated
- HTTP 500 "not activated" on `/bwsearch` → BW Search not activated

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
- The standard `sapse/abap-cloud-developer-trial` image is ABAP Cloud only — no BW Modeling API.
  These steps apply to a full SAP BW/4HANA system (on-prem or a4h with BW add-on).

## Issue Tracking

This project uses `bd` (beads) for issue tracking. See `AGENTS.md` for workflow.

```bash
bd ready                          # Find available work
bd show <id>                      # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>                     # Complete work
bd sync --flush-only              # Export to JSONL
```
