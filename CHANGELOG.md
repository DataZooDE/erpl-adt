# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

- **[docs]** A Claude Code plugin in `plugins/erpl-adt/` (install: `/plugin marketplace add DataZooDE/erpl-adt`
  → `/plugin install erpl-adt@erpl-adt`) with five skills (setup, develop, transport, BW, catalog) and two
  commands (`/erpl-adt:edit-activate-test`, `/erpl-adt:release`). `make check-plugin` (and CI, after the
  build) checks every command line in it against the binary's `--help` — spelling only.
- **[docs]** Exit codes: with `--json`, `activate`, `test` and `check` exit 0 whenever the run itself worked;
  judge them by `failed`, `all_passed` / `total_failed` and `findings` / `error_count`. 7 and 8 are run errors.
- **[docs]** README and `docs/cli-usage.md`: `transport list` takes `--owner` (not `--user`); the exit-code
  table lists 3, 4, 9 and 10; `docs/cli-usage.md` points to the `bw`, `catalog` and `login` help.
