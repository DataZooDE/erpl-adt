# Changelog

All notable changes to `erpl-adt` are documented here. Versions follow `vYYYY.MM.DD` (the date of the
release); same-day re-cuts append `.N`. Earlier releases are described in their GitHub release notes.

## Unreleased

- **[docs]** A Claude Code plugin in `plugins/erpl-adt/` (install: `/plugin marketplace add DataZooDE/erpl-adt`
  → `/plugin install erpl-adt@erpl-adt`) with five skills (setup, develop, transport, BW, catalog) and two
  commands (`/erpl-adt:edit-activate-test`, `/erpl-adt:release`). `make check-plugin` checks every command
  line in it against the binary's `--help`.
- **[docs]** README and `docs/cli-usage.md`: `transport list` takes `--owner` (not `--user`); the exit-code
  table lists 3, 4, 9 and 10; `docs/cli-usage.md` points to the `bw`, `catalog` and `login` help.
