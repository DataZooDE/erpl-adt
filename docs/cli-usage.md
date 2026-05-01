# CLI Usage

`erpl-adt` is a CLI and MCP server for the SAP ADT REST API. It provides two-level command dispatch (`erpl-adt <group> <action>`) with support for both human-readable and JSON output.

## Global Flags

| Flag | Description |
|------|-------------|
| `--host` | SAP system hostname (default: localhost) |
| `--port` | SAP system port (default: 50000) |
| `--user` | SAP username (default: DEVELOPER) |
| `--password` | SAP password |
| `--password-env <var>` | Read password from env var (default: SAP_PASSWORD) |
| `--client` | SAP client number (default: 001) |
| `--https` | Use HTTPS |
| `--insecure` | Skip TLS certificate verification |
| `--json` | Output in machine-readable JSON |
| `--timeout <sec>` | Request timeout in seconds |
| `--session-file <path>` | Persist session for lock/write/unlock workflows |
| `--color` / `--no-color` | Force or disable ANSI color output |
| `-v` / `-vv` | INFO / DEBUG logging to stderr |

Credential priority: explicit flags > `--password-env` > `.adt.creds` (via `login`) > `SAP_PASSWORD` env var.

## Command Groups

### search -- Search for ABAP objects

```bash
# Search for classes matching a pattern
erpl-adt search ZCL_MY_* --type CLAS --max 20

# JSON output for scripting
erpl-adt --json search ZCL_MY_* --type CLAS
```

**API:** `SearchObjects(session, options)` -- `GET /sap/bc/adt/repository/informationsystem/search?operation=quickSearch&query=...`

### object -- Object CRUD operations

```bash
# Read object metadata and structure
erpl-adt object read /sap/bc/adt/oo/classes/ZCL_EXAMPLE
erpl-adt object read ZCL_EXAMPLE          # name resolution shorthand

# Create a new class
erpl-adt object create --type CLAS/OC --name ZCL_NEW --package ZTEST --transport NPLK900001

# Delete an object (auto-locks, then deletes)
erpl-adt object delete /sap/bc/adt/oo/classes/ZCL_OLD --transport NPLK900001

# Lock an object
erpl-adt object lock /sap/bc/adt/oo/classes/ZCL_EXAMPLE

# Unlock an object
erpl-adt object unlock /sap/bc/adt/oo/classes/ZCL_EXAMPLE --handle LOCK_HANDLE

# Run an ABAP console class (IF_OO_ADT_CLASSRUN)
erpl-adt object run ZCL_MY_RUNNER
```

**API:**
- `GetObjectStructure(session, uri)` -- `GET {objectUri}`
- `CreateObject(session, params)` -- `POST /sap/bc/adt/{creationPath}`
- `DeleteObject(session, uri, handle)` -- `DELETE {objectUri}?lockHandle=...`
- `LockObject(session, uri)` -- `POST {objectUri}?_action=LOCK`
- `UnlockObject(session, uri, handle)` -- `POST {objectUri}?_action=UNLOCK`

### source -- Source code read/write

```bash
# Read source code (active version)
erpl-adt source read ZCL_MY_CLASS
erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main

# Read inactive version or a specific section
erpl-adt source read ZCL_MY_CLASS --version inactive
erpl-adt source read ZCL_MY_CLASS --section testclasses

# Open source in $EDITOR and write back changes
erpl-adt source edit ZCL_MY_CLASS --transport NPLK900001

# Write source code from a file (auto-locks, writes, unlocks)
erpl-adt source write ZCL_MY_CLASS --file impl.abap --transport NPLK900001

# Write and activate in one step
erpl-adt source write ZCL_MY_CLASS --file impl.abap --transport NPLK900001 --activate

# Run syntax check
erpl-adt source check ZCL_MY_CLASS
```

**API:**
- `ReadSource(session, uri, version)` -- `GET {sourceUri}?version=active`
- `WriteSource(session, uri, source, handle)` -- `PUT {sourceUri}?lockHandle=...`
- `CheckSyntax(session, uri)` -- `POST /sap/bc/adt/checkruns?reporters=abapCheckRun`

### test / check -- ABAP Unit and ATC

```bash
# Run unit tests (by name or URI)
erpl-adt test ZCL_MY_CLASS
erpl-adt --json test ZCL_MY_CLASS

# Run ATC quality checks
erpl-adt check ZCL_MY_CLASS
erpl-adt check ZCL_MY_CLASS --variant FUNCTIONAL_DB_ADDITION
```

**API:**
- `RunTests(session, uri)` -- `POST /sap/bc/adt/abapunit/testruns`
- `RunAtcCheck(session, uri, variant)` -- worklist + run + get findings

### transport -- Transport management

```bash
# List transports for a user
erpl-adt transport list --user DEVELOPER

# Create a new transport
erpl-adt transport create --desc "Feature X implementation" --package ZTEST_PKG

# Release a transport
erpl-adt transport release NPLK900001
```

**API:**
- `ListTransports(session, user)` -- `GET /sap/bc/adt/cts/transportrequests?user=...`
- `CreateTransport(session, desc, pkg)` -- `POST /sap/bc/adt/cts/transports`
- `ReleaseTransport(session, number)` -- `POST /sap/bc/adt/cts/transportrequests/{number}/newreleasejobs`

### package -- Package operations

```bash
# List package contents
erpl-adt package list ZTEST_PKG

# Check if package exists
erpl-adt package exists ZTEST_PKG

# Recursively list all objects in a package hierarchy
erpl-adt package tree ZTEST_PKG
erpl-adt package tree ZTEST_PKG --type CLAS          # filter by object type
erpl-adt package tree ZTEST_PKG --max-depth 10
```

**API:** `ListPackageContents(session, name)` -- `POST /sap/bc/adt/repository/nodestructure`

### ddic -- Data Dictionary operations

```bash
# Get table definition with field lengths and descriptions (default)
erpl-adt ddic table SFLIGHT
erpl-adt --json ddic table MARA

# Skip data-element lookup (fast, offline — field names and types only)
erpl-adt ddic table SFLIGHT --no-resolve-types

# Print raw SAP XML response
erpl-adt ddic table SFLIGHT --raw

# Read CDS view source
erpl-adt ddic cds I_BUSINESSPARTNER
```

By default, `ddic table` resolves field lengths and descriptions by fetching each referenced
data element from `/sap/bc/adt/ddic/dataelements/{name}`. Built-in `abap.*` types (e.g.
`abap.curr(15,2)`) have their length/decimals parsed from the type string without an extra
request. Use `--no-resolve-types` to skip this enrichment.

**API:**
- `GetTableDefinition(session, name, resolve_types)` -- `GET /sap/bc/adt/ddic/tables/{name}`, then `GET /sap/bc/adt/ddic/dataelements/{type}` per unique field type
- `GetCdsSource(session, name)` -- `GET /sap/bc/adt/ddic/ddl/sources/{name}/source/main`

### discover -- Service discovery

```bash
# Discover available ADT services
erpl-adt discover services
erpl-adt discover services --workspace "Object Repository"
```

**API:** `Discover(session)` -- `GET /sap/bc/adt/discovery`

### mcp -- MCP server mode

```bash
# Start MCP server (JSON-RPC 2.0 over stdio)
erpl-adt mcp --host sap.example.com --port 44300 --https
```

The MCP server exposes all operations as tools for AI agent consumption via the Model Context
Protocol (2024-11-05). Communication is line-delimited JSON-RPC 2.0 over stdin/stdout.

**Supported MCP methods:**
- `initialize` -- handshake and capability negotiation
- `tools/list` -- enumerate available tools
- `tools/call` -- execute a tool by name

### deploy -- Legacy deploy workflow

```bash
# Deploy from YAML config
erpl-adt deploy -c config.yaml
```

The deploy workflow is an idempotent state machine: `discover → create package → clone → pull → activate`.

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Connection / Authentication / CSRF error |
| 2 | Package / NotFound error |
| 3 | Clone error |
| 4 | Pull error |
| 5 | Activation error |
| 6 | Lock conflict |
| 7 | Test failure |
| 8 | ATC check error |
| 9 | Transport error |
| 10 | Timeout |
| 99 | Internal error |

## JSON Output

All commands support `--json` for machine-readable output. Tables are emitted as JSON arrays of objects. Errors are emitted as structured JSON to stderr.
