# Classic GUI objects from git: `erpl-adt abapgit`

`erpl-adt abapgit` deploys classic SAP GUI objects — dynpros, GUI status and
titles, transaction codes, domains, data elements, text pools and function
modules — from files in **abapGit's serialization format**, and exports them
back into exactly the same bytes. A repository can be edited in git, linted
with abaplint, pulled with abapGit inside SAP, or deployed headless with
erpl-adt from CI — all on the same files.

```bash
# What would change? (changes no object — use it in pull-request checks)
erpl-adt abapgit status ./erpl-demo --package ZERPL_DEMO

# Create/update everything in dependency order, then activate
erpl-adt abapgit deploy ./erpl-demo --package ZERPL_DEMO --transport A4HK900146

# One object only (any of its files)
erpl-adt abapgit deploy ./erpl-demo/src/zerpl_so_display.prog.xml --package '$TMP'

# Pull changes made in SE51/SE41/SE93 back into git (refreshes the files in place)
erpl-adt abapgit export ./erpl-demo --package ZERPL_DEMO

# Pull one object back (lands in the folder of its package)
erpl-adt abapgit export ./erpl-demo --package ZERPL_DEMO --object PROG:ZERPL_SO_DISPLAY

# Start a repository from objects that already exist
erpl-adt abapgit export ./new-repo --package '$TMP' --object PROG:ZERPL_SO_DISPLAY,TRAN:ZERPL_VA03

# Remove what the files describe (reverse dependency order)
erpl-adt abapgit delete ./erpl-demo --package '$TMP'
```

`<path>` is either the root of an abapGit repository (the folder holding
`.abapgit.xml`) or a single object file. A single file inside a repository is
resolved through the repository scan, so its folder decides its package
(`src/sub/zfoo.prog.xml` → `ZERPL_DEMO_SUB`) and the ignore list applies;
outside a repository, `--package` is used. All commands accept `--json`.
Flags are checked strictly: an unknown flag such as `--dryrun` fails with exit
code 99 and a "did you mean --dry-run?" hint, before anything runs.

## What a deploy does

1. **Scan** the repository like abapGit: `.abapgit.xml` (starting folder,
   PREFIX/FULL folder logic, ignore list, master language), file names
   (`zfoo.prog.xml`, `zfoo.prog.screen_0100.abap`, `#ns#obj.tran.xml`, …),
   sub-folders mapped to sub-packages.
2. **Guard rails**, before anything is written — the same phase for
   `deploy`, `status`, `delete` and `export`:
   * customer objects and packages only — `Y*`, `Z*`, `$…`, or a
     `/NAMESPACE/` that is changeable in this system (SE03, `TRNSPACE`); the
     namespaces are read from the system in the same round trip as the
     objects, and there is no flag to widen them;
   * no SAP packages; a transportable package needs `--transport` (in
     `status` that is only a note, see below);
   * unsupported object types and files are listed, never silently dropped;
   * each type handler validates the files (missing source or flow-logic
     file, parts it cannot write such as TRAN authorization objects, RFC
     function modules) — `status` reports these exactly as `deploy` would.
3. **Read** the current state of every object in one round trip and render it
   exactly as abapGit would.
4. **Compare** byte for byte. Unchanged objects are left alone — re-running a
   deploy is safe and fast. Changed objects are updated in place, part by part
   (`source`, `screen 0200`, `CUA`, `TPOOL`, …).
5. **Drift protection**, per differing part: a part is only overwritten (or,
   with `delete`, removed) if you were provably its last changer, or its
   current system version is an earlier committed version of its file (git
   history). Otherwise the object is refused (exit code 6), naming who changed
   which file. Pull it into git with the export command the message prints
   (`erpl-adt abapgit export <repo> --package P --object TYPE:NAME`) and
   commit it, or overwrite it with `--force`. Where SAP records the last
   changer:

   | Part | Last changer from |
   |---|---|
   | DOMA, DTEL | `DD01L`/`DD04L-AS4USER` |
   | PROG source and attributes | `REPOSRC-UNAM` |
   | PROG text pool | `REPOTEXT-UNAM` |
   | PROG GUI status / titles | `EUDB-VAUTOR` (creator `AUTOR` until first change) |
   | FUGR function module, include | `REPOSRC-UNAM` of that include (`L<fg>U<nn>`, not `SAPL<fg>`) |
   | Dynpros, TRAN, FUGR short text | **none** — SAP keeps no last changer (`D020S`, `TSTC*` have no user field; SE93 writes no version) |

   A part without a recorded changer counts as "unknown user": it is updated
   only when the system version is in the file's git history. So commit what
   you deployed before you change a screen or transaction again (a CI deploy
   from git always satisfies this); a deploy from uncommitted files needs
   `--force` the second time.
6. **Activate** through the standard workbench activation; activation errors
   are reported per object.

Steps 4–6 are batched by phase, so a deploy runs at most **three** helper-class
runs whatever the number of objects:

| Phase | What | How |
|---|---|---|
| read | every object's state, system facts | engine run 1 |
| DDIC | domains, data elements: written and activated | engine run 2 (only if any changed) |
| ADT | function groups, source of existing programs | ADT requests, per object |
| write + activate | new programs, screens, GUI status, text pools, transactions; then one `RS_WORKING_OBJECTS_ACTIVATE` of everything saved | engine run 3 (only if any changed) |

DDIC objects are active before anything that uses them is written or
generated, and programs are activated before the transactions that call them
are written. Failures stay with their object: every operation carries its
owner, an object whose dependency failed earlier in the same run is reported
`skipped: dependency X failed`, and whatever the activation leaves inactive
fails the object it belongs to (with SAP's messages that name it).

Dependency order: `DOMA → DTEL → FUGR (function modules) → PROG (source,
text pool, dynpros, GUI status) → TRAN`.

## How it reaches SAP

ADT has no REST endpoint for dynpros, GUI status or transaction codes. For
those parts erpl-adt writes a **temporary console class** to `$TMP`
(`ZCL_ERPLHLP_<8 hex>`), activates it, runs it through ADT classrun and
deletes it again. The class only calls the workbench function modules the GUI
tools themselves use, so the result is indistinguishable from work done in
SE51/SE41/SE93/SE11:

| Object / part | Read | Write | Delete |
|---|---|---|---|
| Domain, data element (DOMA, DTEL) | `DDIF_DOMA_GET`, `DDIF_DTEL_GET` | `RS_CORR_INSERT`, `DDIF_*_PUT`, `DDIF_*_ACTIVATE` | `RS_DD_DELETE_OBJ` (request via its `corrnum`) |
| Dynpro | `RS_SCREEN_LIST`, `RPY_DYNPRO_READ` | `RPY_DYNPRO_INSERT` | `RS_SCRP_DELETE` |
| GUI status, titles (CUA) | `RS_CUA_INTERNAL_FETCH` | `RS_CUA_INTERNAL_WRITE` (inactive) | |
| Text pool | `READ TEXTPOOL` | `INSERT TEXTPOOL … STATE 'I'` | |
| Activation of dynpros/CUA/text pool | | `RS_WORKING_OBJECTS_ACTIVATE` | |
| Transaction (TRAN) | `TSTC`, `TSTCC`, `TSTCT`, `TSTCP` | `RPY_TRANSACTION_INSERT` | `RPY_TRANSACTION_DELETE` |
| System facts | `sy-uname`, `TRNSPACE`, `TADIR`/`TDEVC` (export: package of an object) | | |

Program and function-module **source** goes through the regular ADT source
endpoints, with the same locking and transport handling as `erpl-adt source
write`.

Payloads travel base64-encoded into the class; user data is never pasted into
ABAP source. The class body is a real ABAP file in this repository
(`src/abap/`), so it can be read and reviewed like any other code. A class
that fails to compile is never run (SAP would otherwise run its previous
version). If a run is interrupted (network, short dump), a leftover helper is
reported by name. Every `abapgit` command quietly removes your own helpers
that are older than two hours; `erpl-adt abapgit cleanup [--older-than 60]`
does it on demand. Only genuine helper names (`ZCL_ERPLHLP_` + 8 hex digits)
owned by the logon user are touched, never a helper young enough to belong to
a run in progress.

## Working in a team

* **Transports** — pass `--transport <request>` for transportable packages;
  changes are recorded on it by the same function modules the GUI tools use.
  Only parts that actually changed are written, so only those are recorded.
  Local packages (`$TMP`, `$…`) need no request.
* **Locks** — objects are locked while they are written, as in the GUI. If a
  colleague holds the object (open in SE51, or locked in another request), the
  object fails with SAP's message and exit code 6; nothing is overwritten.
* **Versions** — everything is written inactive and activated through the
  standard activation, so SE51/SE41/SE80 version management and where-used
  stay intact, and the objects remain editable in the GUI.
* **Master language** — texts are written in the repository's
  `MASTER_LANGUAGE` (from `.abapgit.xml`).
* **CI / pull-request checks** — `status` changes no object, but it does run a
  temporary helper class in `$TMP` (created, run, deleted; stale own helpers
  older than two hours are swept), so the CI user needs the same development
  authorization (`S_DEVELOP`) as for a deploy. Without `--transport`, objects
  in transportable packages get a note ("the deploy needs --transport")
  instead of a refusal; `--strict` makes it a refusal (exit 9). `status`
  returns non-zero if a deploy would fail or refuse something (drift,
  unsupported parts, missing files). `--json` gives one entry per object with
  `action` (`created`, `updated`, `unchanged`, `deleted`, `missing`,
  `exported`, `failed`, `refused`, `skipped`), the written `parts`, SAP's
  `message`, `notes`, and for failures `category` and `exit_code`; the
  top-level `exit_code` is the command's exit code.
* **Failures in dependency order** — when an object fails, later objects whose
  files name it (a data element's domain, a transaction's program) are
  reported as `skipped: dependency DOMA X failed` and not attempted.
* **Progress** — on a terminal (and without `--json`) deploy and delete print
  one line per object to stderr: `[3/8] updating PROG ZERPL_SO_DISPLAY`.
* **Export** — a refresh writes every object back into the folder it was
  scanned from; `--object` writes into the folder of the object's package
  (TADIR, mapped with the repository's folder logic) and refuses objects of
  packages outside the repository's root package. Files of parts the system no
  longer has (e.g. a screen deleted in SE51) are removed and reported as
  `removed <file>`, so git shows the deletion.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | everything deployed / unchanged / exported |
| 1 | authorization, guard rail (SAP object, namespace not changeable), connection |
| 2 | object or path not found, package/folder problem |
| 5 | SAP rejected an object (syntax, activation) |
| 6 | lock conflict (someone holds the object) or drift (changed outside git) |
| 8 | the deploy cannot write a part (unsupported part, RFC module) |
| 9 | transport problem (missing `--transport`, object locked in another request) |
| 10 | timeout, or the system answered "too many requests" |
| 99 | usage error (unknown flag, invalid argument) or internal error |

Failures are classified by evidence, never by (translated) message text: the
helper's own verdict from the function module's exception, else SAP's T100
message id (e.g. `MC 601`, `EU 510`, `DS 068` → 6; other `TK`/`TR` → 9).
When several objects fail, the most severe category decides the exit code:
1 > 99 > 10 > 9 > 6 > 5 > 8 > 2 — environment and guard-rail problems first,
then problems that need a human (CTS, someone else's lock or change), then
problems in the code being deployed.

## Limitations

* Supported types: DOMA, DTEL, FUGR (function modules), PROG, TRAN. Classes,
  interfaces, tables (TABL) and packages (DEVC) are not created by `abapgit
  deploy`; create packages with `erpl-adt object create --type DEVC/K`, and
  deploy classes with `erpl-adt source write`.
* Translations (I18N/LXE) and long-text documentation (DOCU) are not
  deployed; only the master language.
* Native dynpros (splitter screens stored as `NAT_HEADER`) and report variants
  (`VARIS`) are reported as unsupported parts.
* The drift check needs `git` on the PATH to consult file history (the last
  20 commits of each file); without it, only parts provably last changed by
  you count as safe — screens and transactions then always need `--force`.
* Transport recording (`--transport`) uses the function modules' own request
  parameters and `RS_CORR_INSERT`, but it has not been verified live: the
  A4H trial's client settings were left unchanged, so no transportable
  package with a request was available for tests.
* Each engine run rewrites, activates and runs the temporary helper class
  (created once per command, deleted at the end - about 3 s together). A
  deploy needs at most three runs (see *What a deploy does*). Measured on the
  A4H trial (demo repository, 8 objects, two runs each): from scratch 10 runs
  / 20.7-23.6 s before batching, 3 runs / 14.4-15.4 s after; a redeploy with
  one changed screen 3 runs / 9.2-11.9 s before, 2 runs / 8.9-10.4 s after.
  Beyond the first run the helper costs about 0.5-1 s per run; the rest is
  SAP's own work (writes, screen generation, activation), which batching
  does not change.
* abapGit's MIXED folder logic is not supported (PREFIX and FULL are).
* A screen whose flow logic SAP rejects (e.g. a `FIELD` that is not on the
  screen) is still saved: `RPY_DYNPRO_INSERT` saves before it generates, as
  SE51 does. The deploy fails with SAP's message (exit 5), and because dynpros
  record no last changer, the next deploy of the fixed file is refused as
  drift until the failed attempt is committed (or with `--force`).
