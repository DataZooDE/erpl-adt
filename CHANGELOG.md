# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

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
- **[feature]** `source check --version inactive` (and `version` on the MCP tool `adt_check_syntax`) checks
  source that was written but not activated yet; the default stays `active`.
- **[fix]** `source write --activate --json` prints one JSON object — `written` plus `activation` (`activated`,
  `failed`, `error_messages`) — instead of a success object followed by activation errors as text on stderr.
- **[docs]** README and `docs/cli-usage.md`: `transport list` takes `--owner` (not `--user`); the exit-code
  table lists 3, 4, 9 and 10; `docs/cli-usage.md` points to the `bw`, `catalog` and `login` help.
