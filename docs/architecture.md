# Architecture

This document describes the architecture of `erpl-adt`, a CLI and MCP server for the SAP ADT REST protocol. It enables AI coding agents and human developers to interact with ABAP systems over HTTP -- searching, reading/writing source code, running tests, managing transports, and more.

## High-Level Architecture

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
                             i_xml_codec  <-- xml_codec   (tinyxml2, legacy)
```

All arrows point downward. No cycles. Every horizontal boundary is a pure abstract interface. The concrete implementations (`adt_session`, `xml_codec`) are injected at construction time and can be replaced with mocks for testing.

Most operation modules parse XML directly with tinyxml2 using shared parser helpers in `src/adt/xml_utils.hpp` and `src/adt/atom_parser.hpp`. `IXmlCodec` remains only for the legacy deploy workflow (`workflow/deploy_workflow` and package bootstrap paths).

Async `202 Accepted + Location` polling behavior is centralized in `src/adt/protocol_kernel.hpp`, so ADT/BW async call sites share the same timeout, header-contract, and error-shaping semantics.

## Module Map

### Core Layer

#### core/types

Strong types for all domain concepts. Each type is constructed via a static `Create()` factory that validates input and returns `Result<T, string>`. Types are value types: copyable, movable, equality-comparable, and hashable.

| Type | Validation |
|------|------------|
| `PackageName` | Uppercase, max 30 chars, namespace rules |
| `RepoUrl` | Valid HTTPS URL |
| `BranchRef` | Git branch reference (e.g., `refs/heads/main`) |
| `RepoKey` | Non-empty opaque key |
| `SapClient` | Exactly 3 digits |
| `ObjectUri` | Starts with `/sap/bc/adt/` |
| `ObjectType` | Contains `/` separator, e.g., `CLAS/OC` |
| `TransportId` | 4 uppercase letters + 6 digits, e.g., `NPLK900001` |
| `LockHandle` | Non-empty opaque string |
| `CheckVariant` | Non-empty string |
| `SapLanguage` | Exactly 2 uppercase letters |

#### core/result

`Result<T, E>` is a discriminated union (built on `std::variant`) that holds either a value or an error. Provides monadic chaining via `AndThen()` and `Map()`. A `Result<void, E>` specialization exists for operations that succeed with no value.

The `Error` struct carries structured context:

| Field | Type | Description |
|-------|------|-------------|
| `operation` | string | What was being attempted |
| `endpoint` | string | Which ADT endpoint was called |
| `http_status` | optional\<int\> | HTTP status code |
| `message` | string | Human-readable description |
| `sap_error` | optional\<string\> | SAP-specific error text |
| `category` | ErrorCategory | Categorized error type for exit codes |

`ErrorCategory` enum: Connection, Authentication, CsrfToken, NotFound, PackageError, CloneError, PullError, ActivationError, LockConflict, TestFailure, CheckError, TransportError, Timeout, Internal.

### Session Layer

#### adt/i_adt_session (interface)

Abstract HTTP session interface. Defines:

- `Get()`, `Post()`, `Put()`, `Delete()` -- HTTP verbs returning `Result<HttpResponse, Error>`
- `FetchCsrfToken()` -- CSRF token lifecycle
- `PollUntilComplete()` -- async operation polling (202 Accepted + Location header)
- `SetStateful()` / `IsStateful()` -- stateful session support for locking

#### adt/adt_session (implementation)

Concrete `IAdtSession` implementation using cpp-httplib. Handles:

- Basic Auth with persistent credentials
- CSRF token fetch, attach, and refresh on 403
- SAP-specific headers (`sap-client`, `Accept-Language`)
- Cookie-based session management (`sap-contextid` for stateful sessions)
- TLS verification (configurable for self-signed certs)
- Async polling with configurable timeout and interval

### Operation Layer

Each module is a set of free functions taking `IAdtSession&`. No wrapping classes. Internal XML parsing lives in anonymous namespaces.

#### adt/search

| Function | Endpoint |
|----------|----------|
| `SearchObjects(session, options)` | `GET /sap/bc/adt/repository/informationsystem/search` |

Returns `vector<SearchResult>` with name, type, URI, description, package.

#### adt/object

| Function | Endpoint |
|----------|----------|
| `GetObjectStructure(session, uri)` | `GET {objectUri}` |
| `CreateObject(session, params)` | `POST /sap/bc/adt/{creationPath}` |
| `DeleteObject(session, uri, handle)` | `DELETE {objectUri}` |

Supports 10 ABAP object types (CLAS/OC, PROG/P, INTF/OI, DEVC/K, FUGR/F, DDLS/DF, TABL/DT, DTEL/DE, MSAG/N, PROG/I) via an internal type-to-path mapping table.

#### adt/locking

| Function | Endpoint |
|----------|----------|
| `LockObject(session, uri)` | `POST {objectUri}?_action=LOCK` |
| `UnlockObject(session, uri, handle)` | `POST {objectUri}?_action=UNLOCK` |

`LockGuard` is an RAII wrapper that acquires a lock (enabling stateful session), and releases it on destruction.

#### adt/source

| Function | Endpoint |
|----------|----------|
| `ReadSource(session, uri, version)` | `GET {sourceUri}?version=active` |
| `WriteSource(session, uri, source, handle)` | `PUT {sourceUri}?lockHandle=...` |
| `CheckSyntax(session, uri)` | `POST /sap/bc/adt/checkruns` |

#### adt/testing

| Function | Endpoint |
|----------|----------|
| `RunTests(session, uri)` | `POST /sap/bc/adt/abapunit/testruns` |

Returns `TestRunResult` with hierarchical structure: classes -> methods -> alerts.

#### adt/checks

| Function | Endpoint |
|----------|----------|
| `RunAtcCheck(session, uri, variant)` | 3-step: worklist + run + get findings |

Returns `AtcResult` with prioritized findings (1=error, 2=warning, 3=info).

#### adt/transport

| Function | Endpoint |
|----------|----------|
| `ListTransports(session, user)` | `GET /sap/bc/adt/cts/transportrequests` |
| `CreateTransport(session, desc, pkg)` | `POST /sap/bc/adt/cts/transports` |
| `ReleaseTransport(session, number)` | `POST .../transportrequests/{num}/newreleasejobs` |

#### adt/ddic

| Function | Endpoint |
|----------|----------|
| `ListPackageContents(session, name)` | `POST /sap/bc/adt/repository/nodestructure` |
| `GetTableDefinition(session, name)` | `GET /sap/bc/adt/ddic/tables/{name}` |
| `GetCdsSource(session, name)` | `GET /sap/bc/adt/ddic/ddl/sources/{name}/source/main` |

#### adt/discovery, adt/packages, adt/abapgit, adt/activation

Deploy/bootstrap operations. `packages` still uses `IXmlCodec`; `abapgit` and `activation` use tinyxml2 parsing plus protocol-kernel async handling.

### CLI Layer

#### cli/command_router

Two-level command dispatch: `erpl-adt <group> <action> [args]`. Parses global flags (--host, --json, etc.) then routes to registered group/action handlers.

#### cli/output_formatter

Handles human-readable table output and JSON mode. `OutputFormatter` takes a `json_mode` flag and provides `PrintTable()`, `PrintJson()`, `PrintError()`, `PrintSuccess()`.

### MCP Layer

#### mcp/mcp_server

JSON-RPC 2.0 server over stdin/stdout implementing MCP 2024-11-05. Handles `initialize`, `tools/list`, `tools/call` methods.

#### mcp/tool_registry

Registry mapping tool names to handler functions. Each tool has a name, description, JSON Schema for input, and a handler function.

### Config & Workflow Layer

#### config/config_loader

Parses CLI arguments (argparse) and YAML files (yaml-cpp). Merges them with CLI-wins precedence. Resolves `password_env` from environment variables. Detects cycles in `depends_on`.

#### workflow/deploy_workflow

Idempotent state machine for the legacy deploy workflow: discover -> package -> clone -> pull -> activate.

#### workflow/lock_workflow

Shared lock transaction orchestration used by CLI handlers:

- `DeleteObjectWithAutoLock(...)` -> lock, mutate, unlock with RAII
- `WriteSourceWithAutoLock(...)` -> derive object URI, lock, write, unlock

## Directory Structure

```
erpl-adt/
+-- CMakeLists.txt
+-- Makefile
+-- vcpkg.json
+-- include/erpl_adt/
|   +-- core/
|   |   +-- types.hpp              # 11 strong types
|   |   +-- result.hpp             # Result<T,E> + Error + ErrorCategory
|   +-- adt/
|   |   +-- i_adt_session.hpp      # Abstract HTTP session (Get/Post/Put/Delete + stateful)
|   |   +-- adt_session.hpp        # cpp-httplib implementation
|   |   +-- i_xml_codec.hpp        # Abstract XML codec (legacy)
|   |   +-- xml_codec.hpp          # tinyxml2 implementation (legacy)
|   |   +-- discovery.hpp          # Service discovery
|   |   +-- search.hpp             # Object search
|   |   +-- object.hpp             # Object CRUD (read/create/delete)
|   |   +-- locking.hpp            # Lock/unlock + LockGuard RAII
|   |   +-- source.hpp             # Source read/write + syntax check
|   |   +-- testing.hpp            # ABAP Unit test execution
|   |   +-- checks.hpp             # ATC quality checks
|   |   +-- transport.hpp          # Transport management
|   |   +-- ddic.hpp               # Package contents + table def + CDS source
|   |   +-- packages.hpp           # Package operations (legacy)
|   |   +-- abapgit.hpp            # abapGit operations
|   |   +-- activation.hpp         # Mass activation
|   +-- cli/
|   |   +-- command_router.hpp     # Two-level CLI dispatch
|   |   +-- output_formatter.hpp   # Human/JSON output
|   +-- mcp/
|   |   +-- mcp_server.hpp         # MCP JSON-RPC server
|   |   +-- tool_registry.hpp      # Tool name -> handler mapping
|   +-- config/
|   |   +-- app_config.hpp         # Configuration data types
|   |   +-- config_loader.hpp      # CLI + YAML loading
|   +-- workflow/
|       +-- deploy_workflow.hpp    # Legacy deploy state machine
|       +-- lock_workflow.hpp      # Lock transaction use-cases
+-- src/                           # Implementation files (mirrors include/)
+-- test/
|   +-- core/                      # Unit tests for types, result
|   +-- adt/                       # Unit tests for all ADT operations
|   +-- config/                    # Unit tests for config loading
|   +-- workflow/                  # Unit tests for deploy workflow
|   +-- cli/                       # Unit tests for CLI infrastructure
|   +-- mcp/                       # Unit tests for MCP server
|   +-- mocks/                     # Hand-written mock implementations
|   +-- testdata/                  # Captured ADT XML traffic
|       +-- discovery/
|       +-- packages/
|       +-- repos/
|       +-- polling/
|       +-- activation/
|       +-- config/
|       +-- search/
|       +-- object/
|       +-- source/
|       +-- testing/
|       +-- checks/
|       +-- transport/
|       +-- ddic/
+-- docs/                          # Specification and architecture docs
+-- docker/                        # Dockerfile, docker-compose.yaml
+-- scripts/                       # Utility scripts
+-- examples/                      # Example YAML configurations
```

## Build System

CMake + Ninja with vcpkg for dependency management.

- `make release` -- configure and build in Release mode
- `make test` -- build and run all unit tests via CTest
- `make clean` -- remove the build directory

### Dependencies

| Port | Role |
|------|------|
| cpp-httplib[openssl] | HTTP/HTTPS client |
| tinyxml2 | XML parse + build |
| yaml-cpp | Config file parsing |
| argparse | CLI argument parsing |
| nlohmann-json | JSON for MCP protocol |
| openssl | TLS |
| Catch2 | Test framework |

## Error Handling

Every operation returns `Result<T, Error>`. Exceptions are reserved for programming errors only.

`ErrorCategory` maps to exit codes:

| Category | Exit Code |
|----------|-----------|
| Connection, Authentication, CsrfToken | 1 |
| NotFound, PackageError | 2 |
| CloneError | 3 |
| PullError | 4 |
| ActivationError | 5 |
| LockConflict | 6 |
| TestFailure | 7 |
| CheckError | 8 |
| TransportError | 9 |
| Timeout | 10 |
| Internal | 99 |

## Testing Strategy

All tests use Catch2. Mocks are hand-written implementations of `IAdtSession` and `IXmlCodec`. Test fixtures in `test/testdata/` are real captured Eclipse ADT XML traffic.

| Category | Count | SAP needed? |
|----------|-------|-------------|
| Core (types, result) | ~55 | No |
| ADT operations (mock-based) | ~200 | No |
| CLI infrastructure | ~25 | No |
| MCP server | ~15 | No |
| Config + Workflow | ~60 | No |
| **Total** | **~355** | **No** |

## ADT Protocol

The ADT REST protocol is undocumented by SAP. Ground truth comes from captured Eclipse ADT traffic. See `docs/adt-protocol-spec.md` for the full protocol reference.

Key protocol patterns:
- **CSRF tokens:** Every mutating request needs `x-csrf-token`. On 403, re-fetch and retry.
- **Async operations:** Return `202 Accepted` + `Location` header. Poll until completed.
- **Stateful sessions:** `sap-contextid` header for locking operations.
- **Content types:** Vendor-specific MIME types for XML payloads.
