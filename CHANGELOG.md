# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

- **[feature]** Runtime errors (short dumps, ST22) without SAP GUI (#89): `dump list [--owner U] [--since T]
  [--max N]` and `dump show <id> [--full]` (error, exception, short text, where it terminated, and the source
  `uri` + `line` to fix), MCP `adt_list_dumps` / `adt_show_dump`. When `object run` or `adt_run_class` ends in a
  short dump, the error is category `runtime_dump` (exit 99 as before) and its hint names the dump.
- **[feature]** A failed activation says where each error is (#90): `activate --json`, `source write --activate`
  and MCP `adt_activate` add `messages` (type, text, object, include, line, column, uri) beside the unchanged
  `error_messages`; text mode prints `ZCL_X testclasses:9:23: <text>`. Useful when several objects are
  activated together.
- **[feature]** `object create --type TABL/DS` creates dictionary structures (then `source write` the `define structure`
  source, as for TABL/DT); BAPI-like function module interfaces need them.
- **[feature]** `abapgit deploy` deploys remote-enabled function modules (`<REMOTE_CALL>R</REMOTE_CALL>`), which it
  refused before (exit 8). ADT ignores the RFC flag on create; the deploy sets it in the module's metadata under the
  lock, and clears it again when the files drop it. Update function modules stay refused.
- **[feature]** MCP `adt_activate`, `adt_run_tests`, `adt_run_atc` and `adt_check_syntax` report `passed` - the
  verdict the CLI turns into exit 5/7/8, from the same rules (#84). For `adt_check_syntax` the text block stays the
  message list; the structured content is `{result, passed}`.
- **[feature]** Invalid input - a bad flag value, a malformed transport number, an unknown `--version`, a
  misspelled flag on a strict command - is JSON error category `validation` instead of `internal`. The exit code
  stays 99 (#84). The strict-flag refusal is now a complete JSON error (category, exit code, escaped message).
- **[refactor]** `transport show` and MCP `adt_show_transport` share one JSON serializer (#84).
- **[feature]** `transport release <request>` (and MCP `adt_release_transport`) releases the logon user's open
  tasks first, then the request - SAP refuses a request whose task is still open. The released tasks are
  reported (`released_tasks`). Another user's open task stops the release before anything is released.
- **[docs]** `docs/abapgit-deploy.md`: a part erpl-adt cannot write is reported as `failed` with category `check`
  (exit 8), as the JSON says; `refused` is the word for drift, guard rails and namespaces.
- **[fix]** The MCP HTTP server (`mcp --http`, `catalog webui`) read request bodies without a limit - since a
  refused request is read to its end before its 403, a page on any origin could make it buffer an unbounded
  body. Bodies are limited to 16 MB (HTTP 413 above).
- **[feature]** MCP `adt_activate` takes `uris` to activate several objects in one request, as `erpl-adt
  activate a b` does; types and names are derived from the URIs.
- **[fix]** `transport release` accepted a non-empty answer it could not read as a release report as
  "Released"; it is now a transport error (exit 9) that points to `transport show`.
- **[fix]** `activate` said "activated 0, failed 0" (exit 0) when SAP had not activated (#82): for an object
  recorded on a request, SAP may answer with a pre-audit list ("activate these together?") instead. The list
  looks the same whether anything is inactive or not, so erpl-adt now checks the inactive objects: if the
  object or what SAP lists with it is inactive, it exits 5 naming them and the one `erpl-adt activate` call that
  activates them; otherwise it is "nothing to activate". `activate` takes several objects for that call, and
  a URI argument now carries its type - a function group's main program (`…/source/main`) is `FUGR/I SAPL<fg>`,
  which SAP needs to activate it.
- **[fix]** `transport release` (and the MCP tool `adt_release_transport`) said "Released" when SAP had refused
  the release: SAP answers HTTP 200 either way and puts the outcome in a release report. A report other than
  `released` - an open task, an inactive object, a failed export step - is now a transport error (exit 9) with
  SAP's reason, e.g. "Referencing task A4HK900227 not yet released".
- **[fix]** A Transport Organizer refusal had the wrong exit code: a request that does not exist (T100 TR 807) or
  an object already locked in another request (CTS_WBO_API 020, TK 247) came back as `internal` (99) or
  `authorization` (1). They are transport errors (exit 9), as documented, classified by T100 message class.
- **[fix]** `abapgit deploy` of a function group left its main program `SAPL<fg>` inactive, which blocks the
  release of the request it was recorded on. The main program is now activated with its group.
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
