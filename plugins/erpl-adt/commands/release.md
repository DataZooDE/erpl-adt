---
description: Release an SAP transport request with erpl-adt after an explicit review and confirmation — show its metadata, confirm, release, report
argument-hint: "<transport request number> | (empty to choose from your open requests)"
---

Release this transport: $ARGUMENTS

Use the `erpl-adt-transport` skill. Run every command with `--json`.

1. Without a number: `erpl-adt --json transport list` and let the user choose.
2. Show what erpl-adt knows about the request — number, description, owner, status, target. erpl-adt cannot
   list the objects inside a request: say so plainly.
3. Ask the user to confirm they reviewed **this request's contents** in SE09/SE10 or Eclipse. Stop if not.
4. Ask for an explicit release confirmation — releasing exports the request and cannot be undone.
5. `erpl-adt --json transport release <number>`. On exit 9 report SAP's message (open tasks, not owner,
   already released).
6. Report the result.
