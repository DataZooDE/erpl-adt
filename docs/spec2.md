# erpl-adt — Design Document v0.1

**An ADT REST CLI for Humans and AI Coding Agents**

*DataZoo GmbH — February 2026*

---

## 1. Vision

`erpl-adt` is a command-line tool that exposes SAP ABAP Development Tools (ADT) REST API operations as simple, composable CLI commands. It is designed to be used directly by human developers but is primarily optimized as a tool backend for AI coding agents — functioning as an MCP server or as CLI commands invoked by agentic frameworks like Claude Code, Cursor, or custom orchestrators.

The core insight: the ADT REST API is the only clean HTTP interface into the ABAP development workbench. Eclipse uses it, the VS Code ABAP extension uses it, and now AI agents can use it. `erpl-adt` makes this API accessible without Eclipse, without Node.js, without any runtime dependencies — a single static binary per platform.

### 1.1 Strategic Position

The SAP ABAP ecosystem has 300,000+ customers with billions of lines of ABAP code. AI coding assistants cannot reach this code today because ABAP development happens inside proprietary IDEs (Eclipse/ADT, SE80) rather than on local filesystems. `erpl-adt` bridges this gap by giving AI agents the same read-edit-test loop they have for Python/TypeScript/Go, but targeting a live ABAP system over HTTP.

### 1.2 Relationship to erpl-adt

The original `erpl-adt` spec targeted narrow abapGit deployment workflows. `erpl-adt` subsumes this: abapGit deployment becomes one workflow built on the broader ADT primitive operations. The `erpl-adt` functionality is preserved as the `erpl-adt deploy` subcommand group.

### 1.3 Relationship to ERPL Ecosystem

`erpl-adt` is part of the DataZoo ERPL family alongside `erpl` (DuckDB SAP RFC extension), `erpl-web` (DuckDB SAP OData extension), and `flapi` (DuckDB REST API server). It shares the same build infrastructure (CMake + vcpkg + Makefile wrapper + GitHub Actions), the same C++17 design principles, and the same dependency choices where applicable (tinyxml2, cpp-httplib).


---

## 2. Prior Art and Differentiation

### 2.1 Existing ADT Clients

| Project | Language | Scope | Limitations |
|---|---|---|---|
| **abap-adt-api** (Marcello Urbani) | TypeScript/npm | Comprehensive ADT wrapper, MIT licensed | Requires Node.js runtime, library not CLI |
| **mcp-abap-adt** (Mario Andreschak) | TypeScript/npm | Read-only MCP server wrapping abap-adt-api | Read-only by design, requires Node.js |
| **mcp-abap-abap-adt-api** (Mario Andreschak) | TypeScript/npm | Full read-write MCP server, experimental | Marked experimental, requires Node.js, wraps another wrapper |
| **abap-adt-py** | Python | Basic ADT operations | Limited scope, requires Python runtime |
| **Eclipse ADT** | Java/Eclipse | Full IDE, the reference implementation | Heavy GUI, not scriptable, not agent-accessible |

### 2.2 erpl-adt Differentiation

- **Zero runtime dependencies** — single static binary, no Node.js/Python/Java required
- **Dual-mode operation** — CLI for humans, MCP server (JSON-RPC over stdio) for agents, in the same binary
- **Structured output** — every command supports `--json` for machine-parseable results alongside human-readable defaults
- **First-class agent design** — tool descriptions, error codes, and output schemas designed for LLM consumption
- **Cross-platform** — Linux x86_64, macOS arm64/x64, Windows x64 from a single codebase
- **No wrapper-on-wrapper** — direct HTTP calls to ADT endpoints, no intermediary libraries

### 2.3 ADT API Documentation Reality

The ADT REST API is **not officially documented** by SAP. The underlying REST endpoints are internal to Eclipse and SAP provides only a Java SDK for building Eclipse plugins. All non-Eclipse clients (including the VS Code extension) are reverse-engineered from:

1. **ADT Communication Log** — Eclipse can log all HTTP traffic to the ADT backend
2. **mitmproxy / Fiddler** — HTTPS traffic capture between Eclipse and SAP
3. **Discovery document** — `GET /sap/bc/adt/discovery` returns an Atom Service Document listing available endpoints
4. **abap-adt-api source code** — Marcello Urbani's MIT-licensed library is the most complete reverse-engineered reference
5. **abapGit ADT_Backend** — documents the abapGit-specific ADT endpoints

This means: **endpoint payloads must be captured from real Eclipse traffic before implementation.** Phase 0 of development is dedicated to this traffic capture.


---

## 3. ADT REST API Surface

All endpoints live under `/sap/bc/adt/`. Authentication is HTTP Basic over HTTPS. Session management uses CSRF tokens and SAP session cookies.

### 3.1 Authentication & Session Protocol

```
1. GET /sap/bc/adt/discovery
   Headers: x-csrf-token: fetch, Authorization: Basic <base64>
   Response: 200 OK
   Response Headers: x-csrf-token: <token>, set-cookie: <sap-cookies>

2. All subsequent requests carry:
   Headers: x-csrf-token: <token>, Cookie: <sap-cookies>

3. On 403 (CSRF token expired):
   Re-fetch token (step 1), retry original request
```

SAP systems may also require `sap-client` and `sap-language` query parameters or headers.

### 3.2 Endpoint Catalog

The following catalog is organized by capability group. Availability varies by system (on-premise NW 7.4+, S/4HANA, BTP ABAP Environment). The discovery document (`/sap/bc/adt/discovery`) is the authoritative source for what a specific system supports.

#### 3.2.1 Discovery & Navigation

| Endpoint | Method | Purpose |
|---|---|---|
| `/discovery` | GET | Atom Service Document — lists all available ADT capabilities |
| `/repository/nodestructure` | POST | Navigate the object tree (packages, object lists) |
| `/repository/informationsystem/search` | GET | Full-text and pattern search across the ABAP repository |

**Search** is the most important entry point for agents. It accepts `?operation=quickSearch&query=ZCL_*&maxResults=50` and returns object URIs, types, and descriptions. Supports wildcards (`*`), object type filters, and package scoping.

#### 3.2.2 Object Metadata & Source Code

| Endpoint | Method | Purpose |
|---|---|---|
| `/programs/programs/{name}` | GET | Program metadata (title, type, timestamps) |
| `/programs/programs/{name}/source/main` | GET/PUT | Read/write report source code |
| `/oo/classes/{name}` | GET/POST | Class metadata / create class |
| `/oo/classes/{name}/source/main` | GET/PUT | Class implementation source |
| `/oo/classes/{name}/includes/{include}` | GET/PUT | Class sections (definitions, implementations, test classes, macros) |
| `/oo/interfaces/{name}` | GET/POST | Interface metadata / create interface |
| `/oo/interfaces/{name}/source/main` | GET/PUT | Interface source |
| `/functions/groups/{group}` | GET | Function group metadata |
| `/functions/groups/{group}/fmodules/{fm}` | GET | Function module metadata |
| `/functions/groups/{group}/fmodules/{fm}/source/main` | GET/PUT | Function module source |
| `/ddic/ddl/sources/{name}` | GET/PUT | CDS view definitions |
| `/ddic/domains/{name}` | GET | Domain metadata |
| `/ddic/dataelements/{name}` | GET | Data element metadata |
| `/ddic/tables/{name}` | GET | Table/structure definition (field list, keys, data types) |
| `/ddic/structures/{name}` | GET | Structure metadata |

**Content negotiation** matters: `Accept: text/plain` returns raw source code, `Accept: application/xml` or `application/atom+xml` returns structured metadata.

#### 3.2.3 Object Lifecycle

| Endpoint | Method | Purpose |
|---|---|---|
| `/{object-uri}` | POST | Create new ABAP object (type determined by URI path) |
| `/{object-uri}` | DELETE | Delete an ABAP object |
| `/{object-uri}/actions/lock` | POST | Acquire pessimistic lock (returns lock handle) |
| `/{object-uri}/actions/unlock` | POST | Release lock |
| `/activation` | POST | Activate one or more objects (compile and register) |

**Locking is mandatory before writes.** The lock request returns a `lockHandle` string that must be included in subsequent PUT/POST requests as the `X-sap-adt-lockHandle` header. Locks are tied to the user session and expire on logout or timeout.

**Activation** can target a single object or a list of objects (mass activation). The request body is XML listing object URIs. Response indicates success/failure per object with detailed error messages.

#### 3.2.4 Syntax Check & Code Quality

| Endpoint | Method | Purpose |
|---|---|---|
| `/checkruns` | POST | Trigger ATC (ABAP Test Cockpit) check run |
| `/checkruns/{id}` | GET | Retrieve ATC findings |
| `/{object-uri}/actions/syntaxcheck` | POST | Syntax check on a single object |
| `/programs/programs/{name}/actions/prettyprint` | POST | Pretty-print / format source code |
| `/codecompletion/proposal` | POST | Code completion suggestions |

**ATC** is the ABAP equivalent of a linter. Findings include severity (error/warning/info), line number, column, message text, and sometimes quickfix suggestions. This is critical feedback for AI agents.

#### 3.2.5 Unit Testing

| Endpoint | Method | Purpose |
|---|---|---|
| `/abapunit/testruns` | POST | Execute ABAP Unit tests |

The test run request specifies scope (single object, package, transport). The response is structured XML containing:
- Test class and method names
- Pass/fail/skip status per assertion
- Assertion messages and expected vs. actual values
- Runtime duration per method
- Stack traces for failures
- Coverage data (statement, branch, procedure coverage)

**This is the critical feedback loop** that enables autonomous AI coding. The agent writes code, activates it, runs tests, reads structured pass/fail results, and iterates.

#### 3.2.6 Transport Management

| Endpoint | Method | Purpose |
|---|---|---|
| `/cts/transportrequests` | POST | Create a new transport request |
| `/cts/transportrequests/{id}` | GET | Read transport details |
| `/cts/transportrequests/{id}/tasks` | GET/POST | List/create tasks within a transport |
| `/cts/transportrequests/{id}/actions/release` | POST | Release a transport (trigger export) |
| `/cts/transportrequests/{id}/objects` | GET/POST | List/add objects to transport |
| `/cts/transportchecks` | POST | Check transport for consistency |
| `/{object-uri}?_action=GETTRANSPORTINFO` | GET | Get transport info for an object |

Transport management is essential for moving code through the SAP landscape (DEV → QAS → PRD). An agent working on a production-bound change needs to create a transport, assign objects to it, and optionally release it.

#### 3.2.7 Package Management

| Endpoint | Method | Purpose |
|---|---|---|
| `/packages` | POST | Create a new package |
| `/packages/{name}` | GET | Read package metadata |
| `/packages/{name}/subpackages` | GET | List sub-packages |

#### 3.2.8 abapGit Integration

These endpoints require the abapGit plugin to be installed on the SAP system.

| Endpoint | Method | Purpose |
|---|---|---|
| `/abapgit/repos` | GET | List linked abapGit repositories |
| `/abapgit/repos` | POST | Clone/link a Git repository |
| `/abapgit/repos/{id}` | GET | Repository details and status |
| `/abapgit/repos/{id}/actions/pull` | POST | Pull changes from Git |
| `/abapgit/repos/{id}/actions/push` | POST | Push changes to Git |
| `/abapgit/repos/{id}` | DELETE | Unlink repository |
| `/abapgit/repos/{id}/status` | GET | File-level status (modified, added, deleted) |
| `/abapgit/externalrepoinfo/{url}` | POST | Validate external repo URL, list branches |

**Async operations:** Clone and pull return `202 Accepted` with a `Location` header for polling. Poll until `200 OK` (completed) or error.

#### 3.2.9 Refactoring & Code Intelligence

| Endpoint | Method | Purpose |
|---|---|---|
| `/refactorings/rename` | POST | Rename an ABAP object with all references |
| `/refactorings/extractmethod` | POST | Extract code into a new method |
| `/{object-uri}/whereusedlist` | GET | Find all references to an object |
| `/documentation` | GET | ABAP keyword documentation |

#### 3.2.10 Data Dictionary Inspection

| Endpoint | Method | Purpose |
|---|---|---|
| `/ddic/typeinfo` | GET | Type information for a given data type |
| `/ddic/tables/{name}/content` | GET | Table content preview (limited rows) |

### 3.3 Endpoint Availability Matrix

Not all endpoints are available on all system types:

| Capability | NW 7.4+ | NW 7.5+ | S/4HANA | BTP ABAP Env |
|---|---|---|---|---|
| Core (search, source, lock, activate) | ✓ | ✓ | ✓ | ✓ |
| ATC checks | ✓ | ✓ | ✓ | ✓ |
| ABAP Unit | ✓ | ✓ | ✓ | ✓ |
| Transports | ✓ | ✓ | ✓ | Limited |
| abapGit | Plugin | Plugin | Plugin | Built-in |
| CDS views | — | ✓ | ✓ | ✓ |
| Refactoring | — | ✓ | ✓ | ✓ |
| RAP artifacts | — | — | ✓ | ✓ |


---

## 4. Dual-Mode Architecture

`erpl-adt` operates in two modes from a single binary:

### 4.1 CLI Mode (Default)

Human-optimized command-line interface with subcommands, flags, colored output, and progress indicators.

```
$ erpl-adt search "ZCL_SALES*" --type CLAS --max-results 20
$ erpl-adt source read /sap/bc/adt/oo/classes/zcl_sales_order/source/main
$ erpl-adt source write /sap/bc/adt/oo/classes/zcl_sales_order/source/main < modified.abap
$ erpl-adt test run --package ZSALES --json
$ erpl-adt transport create --description "AI: refactor ZCL_SALES_ORDER" --package ZSALES
```

Every command supports `--json` for structured output, making CLI mode usable by shell-scripting agents too.

### 4.2 MCP Server Mode

Started with `erpl-adt mcp`, the binary becomes a Model Context Protocol server communicating over stdio (JSON-RPC 2.0). This is the primary integration path for AI agents.

```json
{
  "mcpServers": {
    "erpl-adt": {
      "command": "/usr/local/bin/erpl-adt",
      "args": ["mcp", "--config", "/path/to/sap-connection.yaml"]
    }
  }
}
```

The MCP server exposes the same operations as CLI subcommands but as MCP tools with structured input schemas and rich output.


---

## 5. CLI Design

### 5.1 Command Structure

```
erpl-adt [global-flags] <command-group> <action> [flags] [args]
```

#### Global Flags

| Flag | Description |
|---|---|
| `--host <host>` | SAP system hostname |
| `--port <port>` | HTTP(S) port (default: 443) |
| `--https / --no-https` | Use HTTPS (default: true) |
| `--client <nnn>` | SAP client number |
| `--user <user>` | SAP username |
| `--password-env <var>` | Environment variable containing password |
| `-c, --config <file>` | YAML configuration file |
| `--json` | JSON output (default for MCP mode) |
| `--log-file <path>` | Write diagnostic log |
| `-v, --verbose` | Verbose output |
| `-q, --quiet` | Suppress non-essential output |
| `--version` | Print version |
| `--timeout <seconds>` | Request timeout (default: 120) |
| `--insecure` | Skip TLS certificate verification |

### 5.2 Command Groups and Actions

#### `discover` — System Capability Discovery

```bash
erpl-adt discover                  # Show all available ADT capabilities
erpl-adt discover --check abapgit  # Check if abapGit endpoints are available
```

#### `search` — Repository Search

```bash
erpl-adt search "ZCL_SALES*"                          # Quick search
erpl-adt search "ZCL_SALES*" --type CLAS              # Filter by object type
erpl-adt search "ZCL_SALES*" --package ZSALES          # Scope to package
erpl-adt search "ZCL_SALES*" --max-results 50 --json   # Machine output
```

#### `source` — Read/Write Source Code

```bash
erpl-adt source read <object-uri>                  # Print source to stdout
erpl-adt source read <object-uri> -o file.abap     # Write to file
erpl-adt source write <object-uri> < file.abap     # Write source from stdin
erpl-adt source write <object-uri> -i file.abap    # Write source from file
erpl-adt source write <object-uri> -i file.abap --activate  # Write + activate
erpl-adt source format <object-uri>                # Pretty-print
```

The `source write` command handles the full lock → write → unlock cycle automatically. With `--activate`, it also activates the object.

#### `object` — Object Lifecycle

```bash
erpl-adt object info <object-uri>                     # Metadata (timestamps, author, status)
erpl-adt object create --type CLAS --name ZCL_NEW --package ZTEST --description "New class"
erpl-adt object delete <object-uri> --transport <id>
erpl-adt object lock <object-uri>                     # Manual lock (returns handle)
erpl-adt object unlock <object-uri>                   # Manual unlock
erpl-adt object activate <object-uri> [<object-uri>...]  # Activate one or more objects
erpl-adt object where-used <object-uri>               # Where-used list
```

#### `test` — ABAP Unit Testing

```bash
erpl-adt test run <object-uri>                # Run tests for a single object
erpl-adt test run --package ZSALES            # Run tests for entire package
erpl-adt test run <object-uri> --json         # Structured test results
erpl-adt test run <object-uri> --coverage     # Include coverage data
```

JSON output schema for test results:
```json
{
  "summary": { "total": 12, "passed": 10, "failed": 1, "skipped": 1, "duration_ms": 342 },
  "classes": [
    {
      "name": "LTC_SALES_ORDER",
      "methods": [
        {
          "name": "test_create_order",
          "status": "passed",
          "duration_ms": 45
        },
        {
          "name": "test_validate_customer",
          "status": "failed",
          "message": "Expected '100' but got '200'",
          "line": 42,
          "details": "Assertion failed in method VALIDATE_CUSTOMER"
        }
      ]
    }
  ]
}
```

#### `check` — Code Quality (ATC)

```bash
erpl-adt check run <object-uri>              # Run ATC checks
erpl-adt check run --package ZSALES          # Package-level checks
erpl-adt check run <object-uri> --json       # Structured findings
erpl-adt check syntax <object-uri>           # Quick syntax check only
```

#### `transport` — Transport Management

```bash
erpl-adt transport list                           # List open transports
erpl-adt transport create --description "..." --package ZSALES
erpl-adt transport info <transport-id>
erpl-adt transport add-object <transport-id> <object-uri>
erpl-adt transport release <transport-id>
erpl-adt transport check <transport-id>
```

#### `package` — Package Management

```bash
erpl-adt package list <parent-package>           # List sub-packages
erpl-adt package info <package-name>             # Package metadata
erpl-adt package create --name ZTEST --description "Test package" --parent $TMP
```

#### `ddic` — Data Dictionary Inspection

```bash
erpl-adt ddic table <table-name>                 # Table structure (fields, keys, types)
erpl-adt ddic structure <structure-name>          # Structure definition
erpl-adt ddic domain <domain-name>               # Domain with value range
erpl-adt ddic dataelement <de-name>              # Data element with search help, texts
erpl-adt ddic typeinfo <type-name>               # Generic type resolution
```

#### `git` — abapGit Operations

```bash
erpl-adt git list                                # List linked repos
erpl-adt git clone <repo-url> --package ZTEST --branch main
erpl-adt git status <repo-id>                    # File-level status
erpl-adt git pull <repo-id>                      # Pull and optionally activate
erpl-adt git push <repo-id>                      # Push to remote
erpl-adt git unlink <repo-id>                    # Unlink repo
```

#### `deploy` — Multi-Repo abapGit Deployment (erpl-adt Compatibility)

```bash
erpl-adt deploy -c deployment.yaml               # Deploy per YAML config
erpl-adt deploy --repo <url> --package ZTEST --branch main  # Single-repo deploy
erpl-adt deploy status                           # Check deployment status
```

This preserves the original `erpl-adt` workflow: create packages → clone repos → pull → activate, with idempotency and `depends_on` ordering from YAML.

#### `completion` — Code Completion (Experimental)

```bash
erpl-adt completion suggest <object-uri> --line 42 --column 10
```

#### `mcp` — MCP Server Mode

```bash
erpl-adt mcp --config connection.yaml            # Start MCP server on stdio
erpl-adt mcp --config connection.yaml --log-file /tmp/erpl-adt.log
```

### 5.3 Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Connection/authentication failure |
| 2 | Object not found |
| 3 | Lock conflict (object locked by another user) |
| 4 | Activation failure |
| 5 | Test failures detected |
| 6 | ATC findings with errors |
| 7 | Transport error |
| 8 | Syntax error in source |
| 10 | Timeout |
| 11 | Capability not available on this system |
| 99 | Internal error |

### 5.4 YAML Configuration

```yaml
# ~/.erpl-adt.yaml or project-local erpl-adt.yaml
connection:
  host: vhcalnplci.local
  port: 8000
  https: false
  client: "001"
  user: DEVELOPER
  password_env: SAP_PASSWORD    # environment variable name

defaults:
  package: ZTEST
  transport_prefix: "AI:"
  timeout: 120
  activate_after_write: true

# For erpl-adt compatibility: multi-repo deployment
deploy:
  repos:
    - url: https://github.com/example/flight-model.git
      branch: main
      package: /DMO/FLIGHT
    - url: https://github.com/example/flight-app.git
      branch: main
      package: /DMO/FLIGHT_APP
      depends_on:
        - /DMO/FLIGHT
```


---

## 6. MCP Server Tool Design

When running as an MCP server (`erpl-adt mcp`), the tool names and schemas are designed for LLM consumption. Tool descriptions are written to help an AI agent understand when and how to use each tool.

### 6.1 Tool Catalog

#### Search & Navigate

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_search` | Search the ABAP repository for objects by name pattern. Use wildcards (*). Returns object URIs needed for all other operations. |
| `adt_discover` | List available ADT capabilities on this SAP system. Call once at session start to understand what operations are supported. |
| `adt_navigate` | Browse the object tree starting from a package. Returns child objects and sub-packages. |

#### Read

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_read_source` | Read the source code of an ABAP object. Returns plain text source. For classes, reads the main implementation. Use includes parameter for specific sections (definitions, testclasses). |
| `adt_read_metadata` | Read metadata for an ABAP object: type, description, timestamps, author, active/inactive status. |
| `adt_read_table` | Read the structure of a data dictionary table or structure: field names, data types, key fields, and descriptions. Essential for understanding data models. |
| `adt_where_used` | Find all places that reference a given ABAP object. Critical for understanding impact before making changes. |

#### Write

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_write_source` | Write source code to an ABAP object. Automatically handles lock/unlock cycle. Provide complete source (not diff). Optionally activates after write. |
| `adt_create_object` | Create a new ABAP development object (class, interface, program, function module, CDS view). Must specify package and optionally transport request. |
| `adt_delete_object` | Delete an ABAP development object. Requires transport request for non-$TMP objects. |
| `adt_activate` | Activate (compile) one or more ABAP objects. Returns activation errors if compilation fails. Always activate after writing source. |

#### Verify

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_run_tests` | Run ABAP Unit tests. Returns structured pass/fail results with assertion messages and line numbers. The primary feedback loop for code changes. |
| `adt_syntax_check` | Quick syntax check without full activation. Returns errors with line/column positions. Faster than activate for catching basic errors. |
| `adt_run_atc` | Run ABAP Test Cockpit (linter) checks. Returns findings with severity, line numbers, and fix suggestions. Use after tests pass to check code quality. |
| `adt_format_source` | Pretty-print / format ABAP source code according to system settings. |

#### Transport

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_create_transport` | Create a new transport request for recording changes. Returns transport ID. Required for non-$TMP packages. |
| `adt_transport_info` | Get transport information for an object: which transport it belongs to, whether it's modifiable. |
| `adt_release_transport` | Release a transport request, triggering export to downstream systems. Irreversible — use with caution. |

#### abapGit

| Tool Name | Description (shown to LLM) |
|---|---|
| `adt_git_clone` | Clone a Git repository into an ABAP package via abapGit. Async operation — returns status. |
| `adt_git_pull` | Pull latest changes from Git into linked ABAP package. Async operation. |
| `adt_git_status` | Check file-level sync status between Git and ABAP objects. |

### 6.2 Tool Input/Output Schemas

Each tool has a JSON Schema for inputs and a structured output format. Example for `adt_run_tests`:

**Input schema:**
```json
{
  "type": "object",
  "properties": {
    "scope": {
      "type": "string",
      "description": "Object URI or package name to test",
      "examples": ["/sap/bc/adt/oo/classes/zcl_sales_order", "ZSALES"]
    },
    "scope_type": {
      "type": "string",
      "enum": ["object", "package"],
      "default": "object"
    },
    "with_coverage": {
      "type": "boolean",
      "default": false
    }
  },
  "required": ["scope"]
}
```

**Output:** Structured JSON with summary counts, per-method results, assertion details, and optionally coverage percentages. (Same schema as CLI `--json` output shown in Section 5.2.)

### 6.3 Agent Workflow Patterns

The MCP tool descriptions and schemas are designed to support these autonomous agent patterns:

**Pattern 1: Read-Edit-Test Loop (primary)**
```
adt_search → adt_read_source → [agent modifies code] → adt_write_source(activate=true)
  → adt_run_tests → if failures: read errors, modify, repeat
  → adt_run_atc → if findings: assess, fix critical ones
```

**Pattern 2: Explore-Understand**
```
adt_search → adt_read_metadata → adt_read_source → adt_where_used
  → adt_read_table (for data model context)
  → [agent builds understanding, answers questions]
```

**Pattern 3: Create-From-Scratch**
```
adt_create_transport → adt_create_object(type=CLAS) → adt_write_source
  → adt_activate → adt_run_tests → adt_run_atc
```

**Pattern 4: Multi-Object Refactoring**
```
adt_search (find affected objects) → adt_where_used (impact analysis)
  → for each object: adt_read_source → modify → adt_write_source
  → adt_activate (mass activation of all modified objects)
  → adt_run_tests (package-level) → adt_run_atc
```


---

## 7. Architecture

### 7.1 Component Diagram

```
┌─────────────────────────────────────────────────┐
│                   erpl-adt                       │
│                                                  │
│  ┌──────────┐    ┌──────────┐    ┌───────────┐  │
│  │ CLI      │    │ MCP      │    │ Config    │  │
│  │ Frontend │    │ Server   │    │ Loader    │  │
│  │ (argparse)│    │ (jsonrpc)│    │ (yaml-cpp)│  │
│  └────┬─────┘    └────┬─────┘    └─────┬─────┘  │
│       │               │               │         │
│       └───────┬───────┘               │         │
│               ▼                       │         │
│  ┌────────────────────────┐           │         │
│  │   Operation Layer      │◄──────────┘         │
│  │                        │                     │
│  │  search · source       │                     │
│  │  object · test         │                     │
│  │  check  · transport    │                     │
│  │  package · ddic        │                     │
│  │  git    · deploy       │                     │
│  └───────────┬────────────┘                     │
│              │                                  │
│  ┌───────────▼────────────┐                     │
│  │   ADT Session          │                     │
│  │                        │                     │
│  │  IAdtSession (abstract)│                     │
│  │  AdtSession (httplib)  │                     │
│  │  MockAdtSession (test) │                     │
│  │                        │                     │
│  │  CSRF token mgmt       │                     │
│  │  Cookie handling        │                     │
│  │  Retry logic           │                     │
│  │  Async polling          │                     │
│  └───────────┬────────────┘                     │
│              │                                  │
│  ┌───────────▼────────────┐                     │
│  │   XML Codec            │                     │
│  │                        │                     │
│  │  IXmlCodec (abstract)  │                     │
│  │  XmlCodec (tinyxml2)   │                     │
│  │  MockXmlCodec (test)   │                     │
│  └────────────────────────┘                     │
│                                                  │
│  ┌────────────────────────┐                     │
│  │   Core Types           │                     │
│  │                        │                     │
│  │  Result<T,E>           │                     │
│  │  Strong types          │                     │
│  │  Error model           │                     │
│  └────────────────────────┘                     │
└─────────────────────────────────────────────────┘
```

### 7.2 Design Seams

Two abstract interfaces define the testability boundaries:

**IAdtSession** — abstracts HTTP communication with the SAP system
- `login()`, `logout()`
- `get()`, `post()`, `put()`, `delete()` — with headers, body, content-type
- `csrfToken()`, `cookies()`
- `pollAsync()` — poll a Location URL until completion

**IXmlCodec** — abstracts XML parsing and building
- `parse()` — XML string → domain objects
- `build()` — domain objects → XML string

Operations (search, source, test, etc.) are **free functions** taking `IAdtSession&` — stateless, no unnecessary classes. The session holds the connection state; the operations hold none.

### 7.3 MCP Protocol Layer

The MCP server layer is a thin adapter:
1. Reads JSON-RPC requests from stdin
2. Maps `tools/call` → operation function
3. Serializes result → JSON-RPC response on stdout
4. Handles `tools/list` by returning the tool catalog with schemas

JSON-RPC parsing uses a lightweight approach (direct JSON parsing with a small library or hand-rolled), avoiding heavy framework dependencies. The MCP protocol subset needed is minimal: `initialize`, `tools/list`, `tools/call`, `notifications/initialized`.

### 7.4 Error Model

Every operation returns `Result<T, AdtError>` where `AdtError` carries:

```cpp
struct AdtError {
    ErrorCategory category;    // connection, authentication, not_found, lock_conflict,
                               // activation, test_failure, atc, transport, syntax,
                               // timeout, capability, internal
    int exit_code;             // maps to CLI exit codes
    std::string message;       // human-readable
    std::string details;       // SAP error XML, stack trace, etc.
    std::optional<std::string> object_uri;  // which object failed
    std::optional<int> line;   // for syntax/test errors
    std::optional<int> column;
};
```

In CLI mode, errors print human-readable messages to stderr and set the exit code. In MCP mode, errors are returned as structured JSON in the tool response with `isError: true`.


---

## 8. Build System & Dependencies

### 8.1 Build Infrastructure

Identical to flapi/erpl-adt pattern:

| Component | Choice |
|---|---|
| Language | C++17 |
| Build system | CMake 3.14+ with Ninja generator |
| Package manager | vcpkg (manifest mode, submodule) |
| Build wrapper | Makefile (targets: `debug`, `release`, `test`, `clean`) |
| CI/CD | GitHub Actions (matrix build) |
| Test framework | Catch2 |

### 8.2 Dependencies (vcpkg ports)

| Library | Purpose | Rationale |
|---|---|---|
| cpp-httplib[openssl] | HTTP/HTTPS client | Same as DuckDB, header-only |
| tinyxml2 | XML DOM parsing | Same as erpl-web, lightweight |
| yaml-cpp | YAML config parsing | Same as flapi |
| argparse | CLI argument parser | Same as flapi |
| openssl | TLS support | Required by cpp-httplib |
| nlohmann-json | JSON parsing/serialization | For MCP protocol and `--json` output |

**New compared to erpl-adt:** `nlohmann-json` is added for MCP JSON-RPC handling and structured CLI output. This is the de-facto standard C++ JSON library (header-only, vcpkg available).

**Explicitly NOT included:** Boost, Crow, pugixml, any mocking framework, any MCP framework (protocol is simple enough to handle directly).

### 8.3 Cross-Platform Targets

| Target | Runner | Triplet | Linking |
|---|---|---|---|
| Linux x86_64 | ubuntu-24.04 | x64-linux | Static libgcc/libstdc++, static OpenSSL |
| macOS arm64 | macos-14 | arm64-osx | vcpkg static, dynamic system frameworks |
| macOS x86_64 | macos-13 | x64-osx | Same |
| Windows x64 | windows-2022 | x64-windows-static | Static CRT (/MT) |

Single static binary per platform, zero runtime dependencies, target size ~5-10 MB.


---

## 9. C++17 Design Principles

Hard constraints carried over from erpl-adt spec, applied consistently:

- **RAII everywhere** — no raw `new`/`delete`, `std::unique_ptr`/`shared_ptr`, Rule of Zero for value types
- **Result<T,E>** — no exceptions for control flow, structured errors with context
- **Strong types** — `ObjectUri`, `PackageName`, `TransportId`, `LockHandle` — not bare strings
- **Const-correctness** — all non-mutating methods `const`, all non-modified params `const&`
- **Value semantics** — prefer value types, move semantics for large objects
- **`std::string_view`** for non-owning access
- **`std::optional`** for optional values
- **No implicit conversions**, no C-style casts, no `void*`
- **Pure abstract interfaces** — one level deep, constructor injection
- **Free functions over classes** for stateless operations


---

## 10. Project Layout

```
erpl-adt/
├── CMakeLists.txt
├── Makefile                              # make debug|release|test|clean
├── vcpkg.json                            # manifest with pinned baseline
├── vcpkg/                                # submodule
│
├── .github/workflows/
│   ├── build.yaml                        # CI: all OS, unit tests
│   └── release.yaml                      # tagged release: binaries + checksums
│
├── include/erpl_adt/
│   ├── core/
│   │   ├── types.hpp                     # ObjectUri, PackageName, TransportId, LockHandle, ...
│   │   ├── result.hpp                    # Result<T,E>
│   │   ├── error.hpp                     # AdtError, ErrorCategory
│   │   └── log.hpp                       # Logging
│   │
│   ├── session/
│   │   ├── i_adt_session.hpp             # Abstract session interface
│   │   ├── adt_session.hpp               # cpp-httplib implementation
│   │   └── session_config.hpp            # Connection parameters
│   │
│   ├── xml/
│   │   ├── i_xml_codec.hpp               # Abstract XML interface
│   │   └── xml_codec.hpp                 # tinyxml2 implementation
│   │
│   ├── ops/                              # Free functions, one header per operation group
│   │   ├── discovery.hpp
│   │   ├── search.hpp
│   │   ├── source.hpp
│   │   ├── object.hpp
│   │   ├── test.hpp
│   │   ├── check.hpp
│   │   ├── transport.hpp
│   │   ├── package.hpp
│   │   ├── ddic.hpp
│   │   └── git.hpp
│   │
│   ├── workflow/
│   │   └── deploy_workflow.hpp           # Multi-repo deployment orchestration
│   │
│   ├── mcp/
│   │   ├── mcp_server.hpp                # JSON-RPC stdio server
│   │   ├── tool_registry.hpp             # Tool catalog with schemas
│   │   └── tool_handler.hpp              # Maps tool calls to ops/
│   │
│   └── config/
│       ├── app_config.hpp                # Merged CLI + YAML config
│       └── config_loader.hpp             # YAML parsing
│
├── src/                                  # Mirrors include/ structure
│   ├── main.cpp
│   ├── core/
│   ├── session/
│   ├── xml/
│   ├── ops/
│   ├── workflow/
│   ├── mcp/
│   └── config/
│
├── test/
│   ├── core/                             # Pure unit tests (no SAP needed)
│   ├── session/                          # Mock-based session tests
│   ├── xml/                              # XML codec tests against captured data
│   ├── ops/                              # Operation tests with mocked session
│   ├── workflow/                         # Deployment workflow tests
│   ├── mcp/                              # MCP protocol tests
│   ├── mocks/
│   │   ├── mock_adt_session.hpp          # Hand-written mock
│   │   └── mock_xml_codec.hpp
│   └── testdata/                         # Captured ADT XML traffic
│       ├── discovery.xml
│       ├── search_response.xml
│       ├── class_source.txt
│       ├── test_results.xml
│       ├── atc_findings.xml
│       ├── activation_error.xml
│       └── transport_info.xml
│
├── scripts/
│   ├── capture-traffic.sh                # mitmproxy setup for Phase 0
│   └── smoke-test.sh                     # Live system smoke test
│
├── docker/
│   ├── Dockerfile                        # Multi-stage build
│   └── docker-compose.yaml               # SAP trial + erpl-adt
│
└── examples/
    ├── connection.yaml
    ├── deployment.yaml
    └── agent-workflow.md                  # Example agent session transcript
```


---

## 11. Development Phases (TDD)

### Phase 0: Traffic Capture (No Code)

Capture ADT XML traffic from Eclipse using mitmproxy. Store in `test/testdata/`. Capture at minimum:
- Discovery document
- Search requests/responses (various object types)
- Class source read/write cycle (GET, lock, PUT, unlock, activate)
- ABAP Unit test run (request + response with pass/fail)
- ATC check run (request + response with findings)
- Transport create and object assignment
- Package creation
- Error responses (404, 403, lock conflict, activation error)
- abapGit clone/pull/status (if available)

### Phase 1: Core Types & Result

`core/types.hpp`, `core/result.hpp`, `core/error.hpp`
- Strong types with explicit constructors and `std::string_view` access
- `Result<T,E>` with monadic `map()`, `and_then()`, `or_else()`
- `AdtError` with category, exit code, message, details
- 100% unit tested offline

### Phase 2: XML Codec

`xml/xml_codec.hpp` — parse and build ADT XML using captured testdata
- Parse discovery document → capability list
- Parse search response → vector of search results
- Parse test results XML → structured test results
- Parse ATC findings → structured findings
- Build activation request XML from object URI list
- Build object creation XML
- All tested against captured traffic, no network

### Phase 3: ADT Session

3a: **Unit tests with MockAdtSession** — CSRF token fetch, cookie management, 403 retry, async polling loop, lock/unlock sequencing

3b: **Live smoke test** — connect to real system, fetch discovery document, run one search

### Phase 4: Operations (the bulk)

Implement ops/ one group at a time, each with mock-based unit tests first, then live integration tests:

4a: `discovery` — parse capability document, check for specific features
4b: `search` — repository search with filters
4c: `source` — read/write with lock/unlock cycle
4d: `object` — create, delete, activate, where-used
4e: `test` — ABAP Unit test execution and result parsing
4f: `check` — ATC and syntax check
4g: `transport` — create, info, add-object, release
4h: `package` — create, info, list
4i: `ddic` — table/structure/domain/dataelement metadata
4j: `git` — abapGit operations (if endpoints available)

### Phase 5: Config & CLI Frontend

- YAML config loading and merging with CLI flags
- argparse command structure (all subcommands)
- Human-readable output formatting
- JSON output mode
- Exit code mapping

### Phase 6: MCP Server

- JSON-RPC 2.0 over stdio
- Tool catalog generation from tool_registry
- Tool call dispatching to ops/
- Structured error responses
- Integration test: simulate an agent session

### Phase 7: Deploy Workflow

- Multi-repo deployment with `depends_on` ordering
- Idempotency (skip already-completed steps)
- YAML deployment config
- Mock-based unit tests + live E2E test

### Phase 8: Cross-Platform Build & CI

- GitHub Actions matrix for all 4 targets
- vcpkg binary caching
- Release workflow with checksums
- Docker image

### Test Boundaries

Phases 1, 2, 3a, 5 (config parsing), 6 (protocol), 7a (mock) run in CI on every commit — no SAP system required. These form the offline test suite.

Phases 3b, 4 (integration), 7b run on-demand against a live SAP system.


---

## 12. Competitive Landscape & Positioning

### 12.1 Why Not Use the Existing MCP Servers?

The existing mcp-abap-adt servers (Mario Andreschak) are valuable proofs-of-concept but have deployment friction:
- Require Node.js runtime
- Wrap abap-adt-api which wraps axios which wraps HTTP — three layers of abstraction
- No cross-platform static binaries
- Experimental stability status (the read-write version)

For enterprise deployment to thousands of developer workstations or CI/CD pipelines, a zero-dependency static binary is a significant advantage.

### 12.2 Why Not Use abap-adt-api Directly?

Marcello Urbani's library is excellent and MIT-licensed. It's the best reference for understanding ADT endpoints. But for the CLI/MCP use case:
- Library, not a tool — requires writing a Node.js application around it
- TypeScript ecosystem dependency
- Not designed for agent consumption (no MCP, no structured output schemas)

### 12.3 Market Position

`erpl-adt` occupies a unique position: **the only standalone, zero-dependency CLI/MCP tool for programmatic ABAP development.** It enables:

1. **AI-assisted ABAP development** — Claude Code, Cursor, Copilot agents working on ABAP
2. **CI/CD integration** — ABAP object deployment, testing, and quality checks in pipelines
3. **Cross-platform scripting** — DevOps automation without Eclipse or SAP GUI
4. **Enterprise standardization** — one tool for human and machine ABAP development workflows


---

## 13. Scope

### In Scope (v1.0)

- Core ADT operations: search, source read/write, object lifecycle, activation
- ABAP Unit test execution with structured results
- ATC check execution with structured findings
- Syntax check
- Transport management (create, assign, release)
- Package management
- Data dictionary inspection (tables, structures, domains, data elements)
- CLI with `--json` output
- MCP server mode (stdio JSON-RPC)
- YAML configuration
- Cross-platform static binaries (Linux x64, macOS arm64/x64, Windows x64)
- GitHub Actions CI/CD

### In Scope (v1.1)

- abapGit operations (clone, pull, push, status)
- Multi-repo deployment workflow (erpl-adt compatibility)
- Code completion support
- Refactoring operations (rename, extract method)
- CDS view-specific operations
- RAP artifact support (service bindings, behavior definitions)
- Docker image

### Out of Scope

- GUI / TUI
- Runtime data access (OData, RFC, SQL) — that's what erpl / erpl-web are for
- Basis administration (SM21, ST22, user management)
- Debugging (step-through debugging over HTTP is theoretically possible but impractical)
- SAP GUI transaction emulation
- abapGit as a Git replacement — it's a deployment mechanism, not a VCS


---

## 14. Open Questions

1. **JSON library choice:** nlohmann-json is the obvious choice (header-only, well-tested, vcpkg available). Alternative: simdjson for performance, but nlohmann's API is more ergonomic for building JSON, which MCP mode does heavily.

2. **MCP protocol version:** Target MCP 2024-11-05 (current stable). The subset needed is small: initialize, tools/list, tools/call. No resources, no prompts, no sampling.

3. **Multi-system support:** Should the MCP server support connecting to multiple SAP systems simultaneously? The CLI naturally supports this via `--config`, but MCP tools typically assume a single backend. Decision: v1.0 targets single system; v1.1 considers multi-system via tool name prefixing.

4. **Streaming for long operations:** abapGit pull and mass activation can take minutes. Should MCP mode stream progress? MCP supports notifications but not all clients handle them. Decision: v1.0 blocks and returns final result; logs progress to log file.

5. **License model:** Open-source (MIT like abap-adt-api and mcp-abap-adt)? Or proprietary as part of the ERPL commercial offering? Impacts community adoption vs. revenue.

6. **BTP ABAP Environment authentication:** BTP uses OAuth2 instead of Basic Auth. Support in v1.0 or v1.1? Decision: v1.0 targets on-premise (Basic Auth). v1.1 adds OAuth2 for BTP.


---

## Appendix A: ADT Object Type Codes

Common object type codes used in search and create operations:

| Code | Object Type |
|---|---|
| `PROG/P` | ABAP Program (Report) |
| `CLAS/OC` | ABAP Class |
| `INTF/OI` | ABAP Interface |
| `FUGR/F` | Function Group |
| `FUNC/FF` | Function Module |
| `TABL/DT` | Database Table |
| `DTEL/DE` | Data Element |
| `DOMA/DD` | Domain |
| `VIEW/DV` | Database View |
| `DDLS/DF` | CDS View (Data Definition) |
| `DCLS/DL` | CDS Access Control |
| `SRVB/SVB` | Service Binding (RAP) |
| `SRVD/SRV` | Service Definition (RAP) |
| `BDEF/BDO` | Behavior Definition (RAP) |
| `MSAG/N` | Message Class |
| `TRAN/T` | Transaction |
| `DEVC/K` | Package |

## Appendix B: Key References

1. **abap-adt-api** — https://github.com/marcellourbani/abap-adt-api (MIT) — the most complete reverse-engineered ADT client, primary reference for endpoint discovery
2. **mcp-abap-abap-adt-api** — https://github.com/mario-andreschak/mcp-abap-abap-adt-api — MCP server wrapping abap-adt-api, reference for tool design
3. **abapGit ADT Backend** — https://github.com/abapGit/ADT_Backend — documents abapGit-specific ADT endpoints
4. **DSAG ADT Handbook** — https://impulsant-dsag.de — comprehensive guide for ADT in Eclipse
5. **abap-adt-py** — https://libraries.io/pypi/abap-adt-py — Python ADT client, alternative reference
6. **MCP Specification** — https://modelcontextprotocol.io — Model Context Protocol specification
