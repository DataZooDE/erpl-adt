---
description: Release an SAP transport request with erpl-adt after an explicit review and confirmation — show its tasks and objects, confirm, release, report
argument-hint: "<transport request number> | (empty to choose from your open requests)"
---

Release this transport: $ARGUMENTS

Use the `erpl-adt-transport` skill. Run every command with `--json`.

1. Without a number: `erpl-adt --json transport list` and let the user choose.
2. `erpl-adt --json transport show <number>` (exit 2 = no such request: stop). Present the request — number,
   description, owner, status, target — its tasks (owner, status) and every object entry
   (`pgmid`/`type`/`name`, from `objects` and each task's `objects`). Point out tasks that are still
   modifiable and tasks owned by someone else: they keep the release from going through. If no objects are
   recorded, say so — the request would ship nothing.
3. Ask the user to confirm **this list** is what should ship. Stop if not.
4. Ask for an explicit release confirmation — releasing exports the request and cannot be undone.
5. `erpl-adt --json transport release <number>`. On exit 9 report SAP's message (open tasks, not owner,
   already released).
6. Report the result.
