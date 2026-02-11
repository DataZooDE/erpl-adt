# CLI Usage

`erpl-adt` is a CLI and MCP server for the SAP ADT REST API. It provides two-level command dispatch (`erpl-adt <group> <action>`) with support for both human-readable and JSON output.

## Global Flags

| Flag | Description |
|------|-------------|
| `--host` | SAP system hostname |
| `--port` | SAP system port (default: 443) |
| `--user` | SAP username |
| `--password` | SAP password |
| `--client` | SAP client number (e.g., `001`) |
| `--json` | Output in machine-readable JSON |
| `--insecure` | Skip TLS certificate verification |
| `--config` | Path to YAML configuration file |

## Command Groups

### search -- Search for ABAP objects

```bash
# Search for classes matching a pattern
erpl-adt search query "ZCL_*" --type=CLAS --max=50

# Search for all objects in a namespace
erpl-adt search query "/NAMESPACE/*"
```

**API:** `SearchObjects(session, options)` -- `GET /sap/bc/adt/repository/informationsystem/search?operation=quickSearch&query=...`

### object -- Object CRUD operations

```bash
# Read object metadata and structure
erpl-adt object read /sap/bc/adt/oo/classes/ZCL_EXAMPLE

# Create a new class
erpl-adt object create --type=CLAS/OC --name=ZCL_NEW --package=ZTEST --transport=NPLK900001

# Delete an object (requires lock)
erpl-adt object delete /sap/bc/adt/oo/classes/ZCL_OLD --transport=NPLK900001

# Lock an object
erpl-adt object lock /sap/bc/adt/oo/classes/ZCL_EXAMPLE

# Unlock an object
erpl-adt object unlock /sap/bc/adt/oo/classes/ZCL_EXAMPLE --handle=LOCK_HANDLE
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
erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main

# Read inactive version
erpl-adt source read /sap/bc/adt/oo/classes/zcl_test/source/main --version=inactive

# Write source code (requires lock + transport)
erpl-adt source write /sap/bc/adt/oo/classes/zcl_test/source/main \
  --file=source.abap --handle=LOCK_HANDLE --transport=NPLK900001

# Run syntax check
erpl-adt source check /sap/bc/adt/oo/classes/zcl_test/source/main
```

**API:**
- `ReadSource(session, uri, version)` -- `GET {sourceUri}?version=active`
- `WriteSource(session, uri, source, handle)` -- `PUT {sourceUri}?lockHandle=...`
- `CheckSyntax(session, uri)` -- `POST /sap/bc/adt/checkruns?reporters=abapCheckRun`

### test -- ABAP Unit testing

```bash
# Run tests for a class
erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST

# Run tests for a package
erpl-adt test run /sap/bc/adt/packages/ZTEST_PKG

# Run with JSON output for CI
erpl-adt test run /sap/bc/adt/oo/classes/ZCL_TEST --json
```

**API:** `RunTests(session, uri)` -- `POST /sap/bc/adt/abapunit/testruns`

### check -- ATC quality checks

```bash
# Run ATC checks with default variant
erpl-adt check run /sap/bc/adt/packages/ZTEST_PKG

# Run with specific check variant
erpl-adt check run /sap/bc/adt/oo/classes/ZCL_TEST --variant=FUNCTIONAL_DB_ADDITION
```

**API:** `RunAtcCheck(session, uri, variant)` -- worklist + run + get findings

### transport -- Transport management

```bash
# List transports for a user
erpl-adt transport list --user=DEVELOPER

# Create a new transport
erpl-adt transport create --desc="Feature X implementation" --package=ZTEST_PKG

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

# Create a package
erpl-adt package create --name=ZTEST_PKG --desc="Test package"
```

**API:** `ListPackageContents(session, name)` -- `POST /sap/bc/adt/repository/nodestructure`

### ddic -- Data Dictionary operations

```bash
# Get table definition
erpl-adt ddic table SFLIGHT

# Read CDS view source
erpl-adt ddic cds ZCDS_VIEW
```

**API:**
- `GetTableDefinition(session, name)` -- `GET /sap/bc/adt/ddic/tables/{name}`
- `GetCdsSource(session, name)` -- `GET /sap/bc/adt/ddic/ddl/sources/{name}/source/main`

### git -- abapGit operations

```bash
# List linked repositories
erpl-adt git list

# Clone a repository
erpl-adt git clone --url=https://github.com/org/repo.git \
  --branch=refs/heads/main --package=ZTEST_PKG

# Pull latest changes
erpl-adt git pull REPO_KEY

# Check repository status
erpl-adt git status REPO_KEY

# Unlink a repository
erpl-adt git unlink REPO_KEY
```

**API:** `ListRepos`, `CloneRepo`, `PullRepo`, `UnlinkRepo` -- `/sap/bc/adt/abapgit/repos`

### deploy -- Legacy deploy workflow

```bash
# Deploy from YAML config
erpl-adt deploy --config=deploy.yaml

# Deploy with inline args
erpl-adt deploy --host=sap.example.com --user=ADMIN --password-env=SAP_PASSWORD
```

### discover -- Service discovery

```bash
# Discover available ADT services
erpl-adt discover
```

**API:** `Discover(session)` -- `GET /sap/bc/adt/discovery`

### mcp -- MCP server mode

```bash
# Start MCP server (JSON-RPC 2.0 over stdio)
erpl-adt mcp --host=sap.example.com --user=ADMIN --password=secret
```

The MCP server exposes all operations as tools for AI agent consumption via the Model Context Protocol (2024-11-05). Communication is line-delimited JSON-RPC 2.0 over stdin/stdout.

**Supported MCP methods:**
- `initialize` -- handshake and capability negotiation
- `tools/list` -- enumerate available tools
- `tools/call` -- execute a tool by name

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
