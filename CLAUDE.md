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
- `adt/{packages,abapgit,activation}.hpp` — Legacy deploy workflow operations (use `IXmlCodec`)
- `cli/command_router.hpp` — Two-level dispatch: `erpl-adt <group> <action> [args]`
- `cli/output_formatter.hpp` — Human-readable table and JSON output
- `mcp/mcp_server.hpp` — JSON-RPC 2.0 server over stdio (MCP 2024-11-05)
- `mcp/tool_registry.hpp` — Tool name → handler mapping
- `config/config_loader.hpp` — Merges CLI args (argparse) + YAML (yaml-cpp) into `AppConfig`
- `workflow/deploy_workflow.hpp` — Idempotent state machine: discover → package → clone → pull → activate

**XML parsing strategy:** New operation modules (search, object, source, testing, checks, transport, ddic) parse XML **directly with tinyxml2** inside anonymous namespaces in their `.cpp` files. They do NOT use `IXmlCodec`. The existing `IXmlCodec` is preserved for legacy deploy workflow operations only.

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

## Issue Tracking

This project uses `bd` (beads) for issue tracking. See `AGENTS.md` for workflow.

```bash
bd ready                          # Find available work
bd show <id>                      # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>                     # Complete work
bd sync --flush-only              # Export to JSONL
```
