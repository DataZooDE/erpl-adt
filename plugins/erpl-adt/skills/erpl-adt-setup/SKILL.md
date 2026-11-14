---
name: erpl-adt-setup
description: >
  Installing and connecting erpl-adt, the command-line client for SAP's ADT REST API (the HTTP interface Eclipse
  ADT uses): uvx erpl-adt / pip install erpl-adt, erpl-adt login and the .adt.creds file, global flags (--host,
  --port, --client, --user, --password-env, --language, --https, --insecure, --timeout, --json, -v/-vv), the
  ERPL_ADT_* / SAP_* environment variables and their precedence, exit codes, and the optional MCP server mode
  (erpl-adt mcp). Use when someone wants to work with ABAP or BW objects from a shell or an agent without Eclipse,
  sets up erpl-adt for the first time, or gets a connection, authentication or exit-code error from it.
user-invocable: false
---

# erpl-adt — Setup Cheat Sheet

erpl-adt talks the same HTTP endpoints Eclipse ADT uses — no Eclipse, no RFC SDK, no JVM. Every command takes
`--json` for machine-readable output; an agent should always use it.

## Critical gotchas

1. **HTTP(S) port, not the RFC port.** ADT runs on the ICM: typically `50000` (HTTP) or `443`/`44300`
   (HTTPS, with `--https`). The RFC gateway port 33NN does not work.
2. **Never put a password on the command line.** Use `--password-env VAR` (default `SAP_PASSWORD`),
   `ERPL_ADT_PASSWORD`, or `erpl-adt login` (stores `.adt.creds` in the current directory).
3. **Precedence**: flags > `--password-env` > environment (`ERPL_ADT_*` beats `SAP_*`) > `.adt.creds` >
   defaults (`localhost:50000`, client `001`, user `DEVELOPER`). A stale `.adt.creds` loses to the environment.
4. **`.adt.creds` is per working directory** and holds credentials — keep it out of version control.
5. **`--insecure` skips TLS verification** — only for trial/self-signed systems.

## Install

```bash
uvx erpl-adt --help          # run without installing (uv)
pip install erpl-adt         # or install the wheel
```

## Connect

```bash
# interactive wizard, writes .adt.creds
erpl-adt login
# flag form
erpl-adt login --host sap.example.com --port 44300 --https --user DEVELOPER
# or per command / environment
export ERPL_ADT_HOST=sap.example.com ERPL_ADT_PORT=44300 ERPL_ADT_USER=DEVELOPER
export SAP_PASSWORD='…'      # read via --password-env (default)
erpl-adt --https --json discover services
erpl-adt logout
```

`erpl-adt --json discover services` is the connection test: it lists the ADT workspaces the system offers.

## Exit codes

| Code | Meaning | Code | Meaning |
|---|---|---|---|
| 0 | Success | 6 | Lock conflict |
| 1 | Connection / authentication | 7 | Unit test failure |
| 2 | Not found | 8 | ATC findings |
| 3 | Clone error | 9 | Transport error |
| 4 | Pull error | 10 | Timeout (raise `--timeout`) |
| 5 | Activation error | 99 | Internal error, or a refused misspelled flag on a strict command |

Errors go to stderr as JSON with `--json`. `-v`/`-vv` log HTTP traffic at INFO/DEBUG to stderr.

## MCP server (optional)

`erpl-adt mcp` serves the same operations as MCP tools (`adt_*`, `bw_*`, `catalog_*`) over stdio, or over HTTP
with `--http` (bind `127.0.0.1`, protect with `--auth-token-env`). Register it in your MCP client if you prefer
tool calls over the CLI; the skills here use the CLI.

See also: `erpl-adt-develop` (ABAP objects), `erpl-adt-transport`, `erpl-adt-bw` (BW modeling),
`erpl-adt-catalog` (metadata catalog).
