---
description: Change an ABAP object safely — read, edit, write, activate, then run ABAP Unit and ATC, judging every step by its JSON fields
argument-hint: "<object name> <what to change> [--transport <request>]"
---

Change this ABAP object: $ARGUMENTS

Use the `erpl-adt-develop` skill (and `erpl-adt-transport` for the request). Run every command with `--json`,
never pass a password on the command line (`erpl-adt-setup`), and **judge outcomes by the JSON fields, not
the exit code** — `activate`, `test` and `check` exit 0 when the run itself worked, even if activation failed,
tests failed or ATC found errors.

1. Locate: `erpl-adt --json search <name> [--type CLAS|PROG|INTF|…]` → take `uri` and `package`. A package
   other than `$TMP` needs a transport request — ask for one or list them (`transport list`).
2. Read every section you will touch into local files (`source read <name> [--section …]`). Do not regenerate
   code you have not read.
3. Edit the local file. Follow the ABAP Cloud rules in `erpl-adt-develop` for generated code.
4. Write **without** activating: `erpl-adt --json source write <name> --file <file> [--section …]
   [--transport …]`. Exit 6 = lock conflict: report who holds the lock and stop.
5. Activate: `erpl-adt --json activate <name>`. If `failed > 0`, read `error_messages`, fix the local file,
   go back to step 4. (`source check` is no gate here: it checks the *active* version, not what you wrote.)
6. `erpl-adt --json test <name>` — passed only if `all_passed` is true; report `total_failed` and the failing
   methods. `erpl-adt --json check <name>` — report the findings by priority.
7. Summarise: the change (diff of the local file before/after), activation result, tests, ATC, and the
   transport used. Do not release the transport — that is `/erpl-adt:release`.
