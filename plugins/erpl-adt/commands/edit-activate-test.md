---
description: Change an ABAP object safely — read, edit, syntax-check, write and activate, then run ABAP Unit and ATC and report
argument-hint: "<object name or ADT URI> <what to change> [--transport <request>]"
---

Change this ABAP object: $ARGUMENTS

Use the `erpl-adt-develop` skill (and `erpl-adt-transport` for the request). Run every command with `--json`;
never pass a password on the command line (`erpl-adt-setup`).

1. Locate: `erpl-adt --json object read <name>` (add `--type` if ambiguous). Note the package; a package
   other than `$TMP` needs a transport request — ask for one or list them (`transport list`).
2. Read every section you will touch into local files (`source read <name> [--section …]`). Do not
   regenerate code you have not read.
3. Make the change in the local file. Follow the ABAP Cloud rules in `erpl-adt-develop` for generated code.
4. `erpl-adt --json source check <name>`; fix until it is clean.
5. `erpl-adt --json source write <name> --file <file> [--section …] [--transport …] --activate`. On exit 5
   read the inactive version and the messages, fix, write again. On exit 6 report who holds the lock.
6. `erpl-adt --json test <name>` and `erpl-adt --json check <name>`. Report failures and ATC findings with
   their messages.
7. Summarise: what changed (diff of the local file before/after), activation, tests, ATC, and the transport
   used. Do not release the transport — that is `/erpl-adt:release`.
