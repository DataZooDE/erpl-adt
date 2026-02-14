# erpl-adt

CLI and MCP server for the SAP ADT REST API — a single binary that talks the same HTTP endpoints Eclipse ADT uses. No Eclipse, no SAP NW RFC SDK, no JVM.

[![Build](https://github.com/datazooDE/erpl-adt/actions/workflows/build.yaml/badge.svg)](https://github.com/datazooDE/erpl-adt/actions/workflows/build.yaml)

Part of the [Datazoo](https://datazoo.de) ERPL family.

## What it does

- **Search and browse** ABAP objects, packages, data dictionary tables, CDS views
- **Read and write** source code with lock management and transport integration
- **Run tests** — ABAP Unit and ATC quality checks from the command line
- **Manage transports** — create, list, and release transport requests
- **MCP server** — expose all capabilities to AI agents over JSON-RPC (MCP 2024-11-05)

Every command accepts `--json` for machine-readable output.

## Quick examples

```bash
# Save connection credentials (prompts for password)
erpl-adt login --host sap.example.com --port 44300 --https --user DEVELOPER

# Search for classes matching a pattern
erpl-adt search ZCL_MY_* --type CLAS --max 20

# Read object metadata and source code
erpl-adt object read /sap/bc/adt/oo/classes/zcl_my_class
erpl-adt source read /sap/bc/adt/oo/classes/zcl_my_class/source/main

# Write source code (auto-locks, writes, unlocks)
erpl-adt source write /sap/bc/adt/oo/classes/zcl_my_class/source/main --file impl.abap

# Write and activate in one step
erpl-adt source write /sap/bc/adt/oo/classes/zcl_my_class/source/main --file impl.abap --activate

# Activate an object by name
erpl-adt activate ZCL_MY_CLASS

# Run unit tests and ATC checks (by name or URI)
erpl-adt test ZCL_MY_CLASS
erpl-adt check ZCL_MY_CLASS --variant DEFAULT

# Create a transport request and release it
erpl-adt transport create --desc "Feature XYZ" --package ZPACKAGE
erpl-adt transport release NPLK900042

# Browse packages and data dictionary
erpl-adt package tree ZPACKAGE --type CLAS
erpl-adt ddic table SFLIGHT
erpl-adt ddic cds I_AIRLINE

# Check syntax
erpl-adt source check /sap/bc/adt/oo/classes/zcl_my_class/source/main
```

## Installation

Download the binary for your platform from the [latest release](https://github.com/datazooDE/erpl-adt/releases/latest), or [build from source](#building-from-source).

| Platform | Architecture |
|----------|-------------|
| Linux    | x86_64      |
| macOS    | arm64, x86_64 |
| Windows  | x64         |

## Full reference

```
erpl-adt - CLI for the SAP ADT REST API

  Talks the same HTTP endpoints Eclipse ADT uses. No Eclipse, no RFC SDK, no JVM.
  All commands accept --json for machine-readable output.

USAGE
  erpl-adt [global-flags] <command> [args] [flags]

SEARCH — Search for ABAP objects
  search <pattern>                        Search for ABAP objects
      --type <type>                       Object type: CLAS, PROG, TABL, INTF, FUGR
      --max <n>                           Maximum number of results

OBJECT — Read, create, delete, lock/unlock ABAP objects
  create                                  Create an ABAP object
      --type <type>                       Object type (e.g., CLAS/OC, PROG/P)  (required)
      --name <name>                       Object name  (required)
      --package <pkg>                     Target package  (required)
      --description <text>                Object description
      --transport <id>                    Transport request number
  delete <uri>                            Delete an ABAP object
      --handle <handle>                   Lock handle (skips auto-lock if provided)
      --transport <id>                    Transport request number
  lock <uri>                              Lock an object for editing
      --session-file <path>               Save session for later unlock
  read <uri>                              Read object structure
  unlock <uri>                            Unlock an object
      --handle <handle>                   Lock handle  (required)
      --session-file <path>               Session file for stateful workflow

SOURCE — Read, write, and check ABAP source code
  check <uri>                             Check syntax
  read <uri>                              Read source code
      --version <version>                 active or inactive (default: active)
  write <uri>                             Write source code
      --file <path>                       Path to local source file  (required)
      --handle <handle>                   Lock handle (skips auto-lock if provided)
      --transport <id>                    Transport request number
      --session-file <path>               Session file for stateful workflow
      --activate                          Activate the object after writing

ACTIVATE — Activate inactive ABAP objects
  activate <name-or-uri>                  Activate an ABAP object

TEST — Run ABAP Unit tests
  test <name-or-uri>                      Run ABAP unit tests

CHECK — Run ATC quality checks
  check <name-or-uri>                     Run ATC checks
      --variant <name>                    ATC variant (default: DEFAULT)

TRANSPORT — List, create, and release transports
  create                                  Create a transport
      --desc <text>                       Transport description  (required)
      --package <pkg>                     Target package  (required)
  list                                    List transports
      --user <user>                       Filter by user (default: DEVELOPER)
  release <number>                        Release a transport

DATA DICTIONARY — Tables and CDS views
  cds <name>                              Get CDS source
  table <name>                            Get table definition

PACKAGE — List contents and check package existence
  exists <name>                           Check if package exists
  list <name>                             List package contents
  tree <name>                             List package contents recursively
      --type <type>                       Filter by object type: CLAS, PROG, TABL, INTF, FUGR
      --max-depth <n>                     Maximum recursion depth (default: 50)

DISCOVER — Discover available ADT services
  services                                Discover ADT services

CREDENTIALS
  login                                     Save connection credentials
  logout                                    Remove saved credentials

GLOBAL FLAGS
  --host <host>                           SAP hostname (default: localhost)
  --port <port>                           SAP port (default: 50000)
  --user <user>                           SAP username (default: DEVELOPER)
  --password <pass>                       SAP password
  --password-env <var>                    Read password from env var (default: SAP_PASSWORD)
  --client <num>                          SAP client (default: 001)
  --https                                 Use HTTPS
  --insecure                              Skip TLS verification (with --https)
  --json                                  JSON output
  --timeout <sec>                         Request timeout in seconds
  --session-file <path>                   Persist session for lock/write/unlock workflows
  --color                                 Force colored output
  --no-color                              Disable colored output
  -v                                      Verbose logging (INFO level)
  -vv                                     Debug logging (DEBUG level)

  Credential priority: flags > --password-env > .adt.creds (via login) > SAP_PASSWORD env var

EXIT CODES
  0  Success          1  Connection/auth     2  Not found
  3  Clone error      4  Pull error          5  Activation error
  6  Lock conflict    7  Test failure        8  ATC check error
  9  Transport error  10 Timeout             99 Internal error
```

## MCP server

erpl-adt includes a built-in MCP server (Model Context Protocol, version 2024-11-05) that exposes all ADT operations as tools over JSON-RPC 2.0 on stdin/stdout. This lets AI agents search, read, write, test, and manage ABAP code directly.

```bash
erpl-adt mcp --host sap.example.com --port 44300 --https
```

Configure it in your MCP client (e.g., Claude Desktop, Claude Code):

```json
{
  "mcpServers": {
    "erpl-adt": {
      "command": "erpl-adt",
      "args": ["mcp", "--host", "sap.example.com", "--port", "44300", "--https"],
      "env": {
        "SAP_PASSWORD": "your_password"
      }
    }
  }
}
```

## Deploy workflow

erpl-adt also includes the original `deploy` workflow for automated abapGit package deployment via YAML configuration:

```bash
cat > config.yaml <<EOF
connection:
  host: localhost
  port: 50000
  use_https: false
  client: "001"
  user: DEVELOPER
  password_env: SAP_PASSWORD

repos:
  - name: flight
    url: https://github.com/SAP-samples/abap-platform-refscen-flight.git
    branch: refs/heads/main
    package: /DMO/FLIGHT
    activate: true
EOF

export SAP_PASSWORD=your_password
erpl-adt deploy -c config.yaml
```

The deploy workflow is an idempotent state machine: `discover → create package → clone → pull → activate`. Each step checks preconditions and skips if already satisfied. Re-running is safe. Supports multi-repo deployments with `depends_on` for topological ordering.

## Building from source

```bash
git clone --recurse-submodules https://github.com/datazooDE/erpl-adt.git
cd erpl-adt
make release
```

Requires CMake 3.21+, Ninja, and a C++17 compiler (GCC 13+, Apple Clang 15+, or MSVC 17+). vcpkg is included as a git submodule.

To run the tests:

```bash
make test                          # Unit tests (offline, no SAP system needed)
make test-integration-py           # Integration tests (requires SAP system)
```

## Docker

```bash
docker build -t erpl-adt .
docker run --rm -v $(pwd)/config.yaml:/config.yaml \
    -e SAP_PASSWORD=your_password \
    erpl-adt deploy -c /config.yaml
```

Or use Docker Compose for end-to-end provisioning with a SAP ABAP Cloud Developer Trial:

```bash
docker compose up
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Datazoo GmbH
