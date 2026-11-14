---
description: Review and release an SAP transport request with erpl-adt — show what it contains, confirm, release, report
argument-hint: "<transport request number> | (empty to choose from your open requests)"
---

Release this transport: $ARGUMENTS

Use the `erpl-adt-transport` skill. Run every command with `--json`.

1. Without a number: `erpl-adt --json transport list` and let the user choose.
2. Show the request: owner, description, target, and its objects/tasks from the list output. Point out
   anything unexpected (objects of other users, `$TMP` remnants, inactive objects).
3. Ask for an explicit confirmation — releasing exports the request and cannot be undone.
4. `erpl-adt --json transport release <number>`. On exit 9 report SAP's message (open tasks, not owner,
   already released).
5. Report the result and the release job.
