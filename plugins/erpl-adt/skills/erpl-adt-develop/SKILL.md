---
name: erpl-adt-develop
description: >
  Developing ABAP from the shell with erpl-adt: finding objects (erpl-adt search --type, package list/tree,
  object read), reading and writing source (source read --version --section, source write --file, then activate;
  source check), activation (activate), ABAP Unit (test) and ATC (check, system check variant by default), creating objects
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
3. **A failed activation exits 5.** `--json activate` (and `source write --activate`, which prints one object
   with `written` and `activation`) carries `failed`, `error_messages` and `messages` (each error's `object`,
   `include` such as `testclasses`, `line`, `column`); the inactive version is saved —
   read it with `--version inactive`, fix, write again. Check what you have just written, before activating,
   with `source check <name> --version inactive` (exit 8 on a syntax error); without `--version` it checks
   the *active* version.
4. **Lock conflicts (exit 6)**: someone (or an earlier aborted run) holds the lock. Commands lock and unlock
   automatically. A manual `object lock` → `source write --handle` → `object unlock --handle` sequence must
   share one private `--session-file` (a global flag; the file holds session cookies — keep it outside the
   repository and delete it afterwards).
5. **ABAP Cloud rules for generated code**: no `MANDT` in `WHERE` (client handling is automatic); a CDS-defined
   table (`TABL/DT`) needs `@AbapCatalog.enhancement.category`, `@AbapCatalog.tableCategory`,
   `@AbapCatalog.deliveryClass` and `@AbapCatalog.dataMaintenance` before `define table`.
6. **`object run` executes code on the server.** Confirm with the user before running a class that changes data.
7. **Field names in `define table` must not be ABAP reserved words.** Activation fails with "MONTHS is a reserved
   word (choose another field name)" — `MONTHS` and `SECONDS` are refused; `TEXT`, `TYPE`, `NAME`, `CLASS`,
   `CODE`, `VERSION`, `STATUS`, `MESSAGE` and `CURRENCY` activate. SAP names only one offending field per
   activation, so a generated table with several gets several rounds: prefer descriptive names (`HIST_MONTHS`,
   `DURATION_S`). `abap.tims`, `abap.int4`, `abap.lang`, `abap.dec(p,d)` and `abap.quan(23,3)` all activate.
8. **The RFC flag of a function module is metadata, not source.** ADT ignores `fmodule:processingType` on create;
   it is set by PUTting the module's metadata under the lock (what Eclipse's properties view does).
   `abapgit deploy` does this for function modules with `<REMOTE_CALL>R</REMOTE_CALL>`; an RFC module's
   parameters must be passed by value. The same for **update modules** (`<UPDATE_TASK>1</UPDATE_TASK>`, V1 start
   immediately): ADT's metadata is `fmodule:processingType="update" fmodule:updateTaskKind="startImmediate"`, and
   the module source SAP exports starts its interface block with `*"*"Update Function Module:` / `*"` before
   `*"*"Local Interface:`. Parameters by value, no EXPORTING; an `XSTRING` IMPORTING parameter is fine (a row
   table serialized with `CALL TRANSFORMATION id`). Other update kinds are refused.
9. **Generated ABAP: what activation refuses on A4H** (SAP names one problem per round, so check before writing):
   method and other names longer than 30 characters; `<word>` in an ABAP Doc comment (`"! ...`) — it is
   parsed as an HTML tag; offset/length access on `STRING` variables in assignments (`result = result(35)`;
   use `substring( )`); `regex =` (POSIX is deprecated; use `pcre =`). A class's `testclasses` section is
   written with `source write <class-uri> --section testclasses`; write every class first and activate them in
   one `activate <uri>...` call, so classes that use each other activate together. Also refused: a table
   expression or offset directly on a method result (`get( )[ key ]`, `get( )+4(2)` — assign it first); a
   positional first argument next to named ones (`assert_true( x msg = ... )` — name `act =`); an unnamed
   argument when the method has more than one optional IMPORTING parameter; a
   `LOOP ... ASSIGNING <fs> WHERE` condition that uses `<fs>`; ABAP Doc (`"!`) before a chained `TYPES:`.
10. **Find where an activation error is.** `--json activate` gives each message's `object`, `include`
    (`main`, `testclasses`, ...), `line` and `column` in `messages`; text mode prints
    `ZCL_X testclasses:70:4: <text>`. Before activating, `--json source check <name> --version inactive` finds
    the same errors in what was just written.
11. **Keep short texts within SAP's field lengths.** `abapgit deploy` refuses the object before writing it
    (exit 8, naming the field and the limit): function group text (`AREAT`) 40, function module text 74, program
    title 70, data element labels 10/20/40 (`SCRTEXT_S/M/L`), short texts 60.
12. **Dictionary structures for RFC parameters:** `object create --type TABL/DS`, then `source write
    /sap/bc/adt/ddic/structures/<name>/source/main` with a `define structure` source and `--activate`. RFC modules
    pass everything by value and need dictionary types (no local types, no `TYPE REF TO`).
13. **ABAP Unit with OSQL test doubles (measured on A4H):** `cl_osql_test_environment` redirects dynamic
    statements too (`INSERT (name) FROM TABLE @<t>`); `ENQUEUE_*` in a test sets real locks of the test session
    (`ENQUEUE_READ` sees them). An exception class inheriting from `CX_DYNAMIC_CHECK` that a method does not
    declare arrives as `CX_SY_NO_HANDLER` (catch that and look at `previous`). Code under test can tell it runs
    in ABAP Unit from the call stack (`SYSTEM_CALLSTACK`: an include `*CCAU` or a `CL_AUNIT*` program).

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
erpl-adt --json source check ZCL_MY_CLASS --version inactive   # exit 8 on a syntax error
erpl-adt --json activate ZCL_MY_CLASS            # exit 5: failed / error_messages
erpl-adt --json test ZCL_MY_CLASS                # exit 7: all_passed / total_failed
erpl-adt --json check ZCL_MY_CLASS                # exit 8 on priority 1: findings / error_count / variant
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

When the class short-dumps, the run exits 99 with category `runtime_dump`, and the hint names the dump. Read it,
fix the line it points at, and run again:

```bash
erpl-adt --json dump list --owner DEVELOPER --max 3   # id, error (e.g. COMPUTE_INT_ZERODIVIDE), program, time
erpl-adt --json dump show <id>                        # exception, include/include_line, and uri + line in the source
```

## Delete

```bash
erpl-adt --json object delete /sap/bc/adt/oo/classes/zcl_old --transport A4HK900001
```

Destructive — confirm with the user first.

See also: `erpl-adt-setup`, `erpl-adt-transport`. Reading the *data* of tables and CDS views from DuckDB is the
erpl plugin (`erpl-rfc-tables`, `erpl-ape-cds`).
