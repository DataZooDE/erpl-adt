# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

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
- **[docs]** Exit codes: with `--json`, `activate`, `test` and `check` exit 0 whenever the run itself worked;
  judge them by `failed`, `all_passed` / `total_failed` and `findings` / `error_count`. 7 and 8 are run errors.
- **[docs]** README and `docs/cli-usage.md`: `transport list` takes `--owner` (not `--user`); the exit-code
  table lists 3, 4, 9 and 10; `docs/cli-usage.md` points to the `bw`, `catalog` and `login` help.
