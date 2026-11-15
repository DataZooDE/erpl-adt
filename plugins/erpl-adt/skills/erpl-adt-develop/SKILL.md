---
name: erpl-adt-develop
description: >
  Developing ABAP from the shell with erpl-adt: finding objects (erpl-adt search --type, package list/tree,
  object read), reading and writing source (source read --version --section, source write --file, then activate;
  source check), activation (activate), ABAP Unit (test) and ATC (check --variant), creating objects
  (object create --type CLAS/OC, PROG/P, INTF/OI, DDLS/DF, TABL/DT, BDEF/BDO, SRVD/SRV, SRVB/SVB with binding
  flags), deleting, locks (object lock/unlock, --session-file), running console classes (object run for
  IF_OO_ADT_CLASSRUN), DDIC inspection (ddic table, ddic cds) and the ABAP Cloud rules agent-written code must
  follow. Use when reading, changing, creating, testing or executing ABAP classes, programs, CDS views or tables
  on an SAP system from a terminal or an agent.
user-invocable: false
---

# erpl-adt — ABAP Development Cheat Sheet

Always add `--json` when an agent parses the output, and judge results by its fields (see `erpl-adt-setup`).
Names resolve to ADT URIs (`ZCL_FOO` → `/sap/bc/adt/oo/classes/zcl_foo`); when a name is ambiguous, find the URI
with `search … --type` (or `source read --type`) and pass the URI.

## Critical gotchas

1. **Read before you write.** `source read` first, edit a local file, then `source write --file …` and
   `--json activate`. Writing replaces the whole section.
2. **Transportable packages need `--transport`.** `$TMP` objects do not; anything else fails without a request
   (`erpl-adt-transport`).
3. **Activation failures are in the JSON, not the exit code.** `--json activate` exits 0 with `failed > 0`
   and `error_messages` when activation fails; the inactive version is saved — read it with
   `--version inactive`, fix, write again. `source check` checks the *active* version only, so it cannot gate
   code you have just written.
4. **Lock conflicts (exit 6)**: someone (or an earlier aborted run) holds the lock. Commands lock and unlock
   automatically. A manual `object lock` → `source write --handle` → `object unlock --handle` sequence must
   share one private `--session-file` (a global flag; the file holds session cookies — keep it outside the
   repository and delete it afterwards).
5. **ABAP Cloud rules for generated code**: no `MANDT` in `WHERE` (client handling is automatic); a CDS-defined
   table (`TABL/DT`) needs `@AbapCatalog.enhancement.category`, `@AbapCatalog.tableCategory`,
   `@AbapCatalog.deliveryClass` and `@AbapCatalog.dataMaintenance` before `define table`.
6. **`object run` executes code on the server.** Confirm with the user before running a class that changes data.

## Find

```bash
erpl-adt --json search 'ZCL_ERPL*' --type CLAS --max 20
erpl-adt --json package tree '$TMP' --type CLAS
erpl-adt --json object read ZCL_MY_CLASS
erpl-adt --json ddic table SFLIGHT
erpl-adt ddic cds ZERPL_APE_FLIGHT
```

## Edit, activate, test

```bash
erpl-adt source read ZCL_MY_CLASS > zcl_my_class.abap
erpl-adt source read ZCL_MY_CLASS --section testclasses > zcl_my_class.testclasses.abap
# … edit …
erpl-adt --json source write ZCL_MY_CLASS --file zcl_my_class.abap --transport A4HK900001
erpl-adt --json activate ZCL_MY_CLASS            # failed / error_messages
erpl-adt --json test ZCL_MY_CLASS                # all_passed / total_failed
erpl-adt --json check ZCL_MY_CLASS --variant DEFAULT   # findings / error_count
```

`source edit` opens `$EDITOR` — interactive, for humans only.

Sections: `main`, `localdefinitions`, `localimplementations`, `testclasses` (`--section all` for reading).
`/erpl-adt:edit-activate-test` runs this loop.

## Create

```bash
erpl-adt --json object create --type CLAS/OC --name ZCL_NEW --package '$TMP' --description 'New class'
erpl-adt --json object create --type DDLS/DF --name ZI_ORDER --package ZTEST --description 'Orders' --transport A4HK900001
erpl-adt --json object create --type SRVB/SVB --name ZORDER_BIND --package ZTEST --description 'Order binding' \
  --binding-type ODATA --binding-version V4 --binding-category 0 --service-definition ZORDER_SRV
```

Then write and activate the source. Creating a service binding does not publish the service.

## Run a console class

```bash
erpl-adt --json object run ZCL_MY_RUNNER     # IF_OO_ADT_CLASSRUN~MAIN output
```

Useful for seeding test data or probing the system — the erpl test harnesses use it this way.

## Delete

```bash
erpl-adt --json object delete /sap/bc/adt/oo/classes/zcl_old --transport A4HK900001
```

Destructive — confirm with the user first.

See also: `erpl-adt-setup`, `erpl-adt-transport`. Reading the *data* of tables and CDS views from DuckDB is the
erpl plugin (`erpl-rfc-tables`, `erpl-ape-cds`).
