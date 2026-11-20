# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

- **[fix]** `object create --type DEVC/K --software-component HOME --transport <req>` was refused by SAP with
  "Change recording must be activated for package ...": the package payload never asked for change recording,
  so no transportable package could be created. A package with a software component other than `LOCAL` now
  records changes (`pak:recordChanges="true"`); local packages are created as before.
- **[fix]** The MCP HTTP server (`mcp --http`, `catalog webui`) could lose its own 403: it refuses a foreign
  Host or Origin before reading the request body, and closing the socket with that body unread made TCP send
  a reset, so the client saw a dropped connection instead of the message naming `--allowed-hosts` /
  `--cors-origin`. Measured with a 2 MB body on Linux (16 of 20 refusals lost); on Windows it happened with a
  small one and failed the web UI smoke test in CI. A refused request with a body is now answered after the
  body is read, still before any handler runs.
- **[feature]** `transport show <number>` (MCP: `adt_show_transport`) lists a request's tasks and the object
  entries recorded on the request and on each task (`pgmid`, `type`, `name`, `description`, `lock_status`),
  so a release can be reviewed from erpl-adt (#76). A task number shows that task; a number that does not
  exist exits 2. `/erpl-adt:release` reviews the request with it instead of asking for SE09.
- **[fix]** Transport numbers whose system ID contains a digit (`A4HK900001` — the trial system is A4H) were
  refused as malformed (`TransportId`), among others by `abapgit deploy --transport`.
- **[docs]** A Claude Code plugin in `plugins/erpl-adt/` (install: `/plugin marketplace add DataZooDE/erpl-adt`
  → `/plugin install erpl-adt@erpl-adt`) with five skills (setup, develop, transport, BW, catalog) and two
  commands (`/erpl-adt:edit-activate-test`, `/erpl-adt:release`). `make check-plugin` (and CI, after the
  build) checks every command line in it against the binary's `--help` — spelling only.
- **[fix]** `check` never reported an ATC finding: the worklist parser looked for unprefixed elements and
  attributes, while SAP prefixes every one (`atcfinding:finding`, `atcfinding:priority`), so every run read as
  clean. Findings now carry the source position (`uri` = `…/source/main#start=line,col`) and the message.
- **[breaking]** Outcome exit codes in text and `--json` mode alike (#75): a failed activation exits 5
  (`activate`, `source write --activate`, `source edit --activate`, which used to print "Activated" anyway),
  a failing unit test 7 (`test`), a priority-1 ATC finding 8 (`check`), a syntax error 8 (`source check`).
  Scripts that ran these and read the JSON fields keep working, but no longer see exit 0 for such outcomes.
- **[fix]** `check` without `--variant` sent the literal `DEFAULT`, which does not exist on A4H; SAP ran other
  checks without a word, and they rate language-scope violations (BREAK-POINT, unreleased APIs) priority 2, so
  exit 8 never fired. It now runs the system check variant (`systemCheckVariant` from the ATC customizing, as
  Eclipse does; `DEFAULT` when none is set), a variant the system does not know is NotFound (exit 2) instead of
  running other checks, and the JSON names the variant that ran (`variant`). Same for the MCP tool `adt_run_atc`.
- **[feature]** `source check --version inactive` (and `version` on the MCP tool `adt_check_syntax`) checks
  source that was written but not activated yet; the default stays `active`.
- **[fix]** `source write --activate --json` prints one JSON object — `written` plus `activation` (`activated`,
  `failed`, `error_messages`) — instead of a success object followed by activation errors as text on stderr.
- **[docs]** README and `docs/cli-usage.md`: `transport list` takes `--owner` (not `--user`); the exit-code
  table lists 3, 4, 9 and 10; `docs/cli-usage.md` points to the `bw`, `catalog` and `login` help.
