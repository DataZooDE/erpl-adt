---
description: Change an ABAP object safely — read, edit, write, check, activate, then run ABAP Unit and ATC, gating every step on its exit code
argument-hint: "<object name> <what to change> [--transport <request>]"
---

Change this ABAP object: $ARGUMENTS

Use the `erpl-adt-develop` skill (and `erpl-adt-transport` for the request). Run every command with `--json`,
never pass a password on the command line (`erpl-adt-setup`), and **gate every step on its exit code**, then
read the JSON for the details: a failed activation exits 5, a failing unit test 7, a syntax error or an ATC
priority-1 finding 8.

1. Locate: `erpl-adt --json search <name> [--type CLAS|PROG|INTF|…]` → take `uri` and `package`. A package
   other than `$TMP` needs a transport request — ask for one or list them (`transport list`).
2. Read every section you will touch into local files (`source read <name> [--section …]`). Do not regenerate
   code you have not read.
3. Edit the local file. Follow the ABAP Cloud rules in `erpl-adt-develop` for generated code.
4. Write **without** activating: `erpl-adt --json source write <name> --file <file> [--section …]
   [--transport …]`. Exit 6 = lock conflict: report who holds the lock and stop.
5. Check what you wrote: `erpl-adt --json source check <name> --version inactive`. Exit 8 = syntax error:
   fix the local file from the messages (`line`, `text`) and go back to step 4.
6. Activate: `erpl-adt --json activate <name>`. Exit 5 = activation failed: read `error_messages`, fix the
   local file, go back to step 4.
7. `erpl-adt --json test <name>` — exit 7 = a test failed; report `total_failed` and the failing methods.
   `erpl-adt --json check <name>` — exit 8 = a priority-1 finding; report the findings by priority.
8. Summarise: the change (diff of the local file before/after), activation result, tests, ATC, and the
   transport used. Do not release the transport — that is `/erpl-adt:release`.
