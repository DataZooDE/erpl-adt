# Classic GUI objects from git: `erpl-adt abapgit`

`erpl-adt abapgit` deploys classic SAP GUI objects — dynpros, GUI status and
titles, transaction codes, domains, data elements, text pools, report
variants and function modules — from files in **abapGit's serialization format**, and exports them
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
Flags are checked strictly (as for every destructive command, see
[cli-usage.md](cli-usage.md#unknown-flags)): an unknown flag such as `--dryrun` fails with exit
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
     function modules, documentation that names another object) — `status`
     reports these exactly as `deploy` would;
   * a file that cannot be read or parsed fails its object, and `status`
     exits non-zero for it even without `--strict`.
3. **Read** the current state of every object in one round trip and render it
   exactly as abapGit would. A read that fails — including a translation or
   documentation read — fails the object; it never reads as "nothing there"
   (an export would drop the texts, a deploy would compare nothing).
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
   | PROG system variants | `VARID-AENAME` (creator `ENAME` until first change), client 000 |
   | FUGR function module, include | `REPOSRC-UNAM` of that include (`L<fg>U<nn>`, not `SAPL<fg>`) |
   | Dynpros, TRAN, FUGR short text | **none** — SAP keeps no last changer (`D020S`, `TSTC*` have no user field; SE93 writes no version) |

   A part without a recorded changer counts as "unknown user": it is updated
   only when the system version is in the file's git history, or is exactly
   what erpl-adt itself last deployed from this clone to this system (see
   *Own deploys* below). A colleague's change in between is still drift.
6. **Activate** through the standard workbench activation; activation errors
   are reported per object.

`status` (a dry run) goes through steps 1–5 and plans step 6 without writing:
it reports the same parts, refusals, planning errors and skipped dependents a
deploy would.

Steps 4–6 are batched by phase, so a deploy runs at most **three** helper-class
runs whatever the number of objects:

| Phase | What | How |
|---|---|---|
| read | every object's state, system facts | engine run 1 |
| DDIC | domains, data elements: written and activated | engine run 2 (only if any changed) |
| ADT | function groups, source of existing programs | ADT requests, per object |
| write + activate | new programs, screens, GUI status, text pools, transactions; then one `RS_WORKING_OBJECTS_ACTIVATE` of everything saved; then report variants (they need the active selection screen) | engine run 3 (only if any changed) |

DDIC objects are active before anything that uses them is written or
generated, and programs are activated before the transactions that call them
are written. Failures stay with their object: every operation carries its
owner, and an object whose prerequisite failed is reported
`skipped: dependency X failed` and not attempted — whatever the prerequisite
failed on: a guard rail, a broken file, the system read, drift, validation,
planning, a write or the activation. Only objects earlier in dependency order
count as prerequisites (a domain for the data element that names it, a
program for its transaction). The worklist activation
(`RS_WORKING_OBJECTS_ACTIVATE` without popup) is all or nothing — one program
with a syntax error cancels it for all; erpl-adt then activates each object
still inactive on its own, so only the broken one stays inactive and fails
(with SAP's messages, matched by whole object name: a message about `ZFOO_TOP`
does not fail `ZFOO`).

### Own deploys

SAP keeps no last changer for screens and transactions, so drift protection
alone would refuse a second deploy of an uncommitted screen change. erpl-adt
therefore remembers the exact bytes it deployed, per SAP system (host, port,
client), object and file, in `.git/erpl-adt/deployed/` of the repository (a
`.git` file of a worktree or submodule is followed; it is never part of a
commit). Outside a git work tree the record lives in
`$XDG_CACHE_HOME/erpl-adt/deployed` (`~/.cache/...`; `%LOCALAPPDATA%` on
Windows). A system version equal to the recorded last deploy counts as your
own; anything else is still refused. The record is per clone: a colleague
deploying from their own clone does not make their version "yours". The MCP
server keeps the record in the repository's git directory only when that
directory lies inside its abapGit root; otherwise it uses its own cache
(`$XDG_CACHE_HOME/erpl-adt/mcp-deployed/<repo>-<hash of the path>`).

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
| Dynpro | `RS_SCREEN_LIST`, `RPY_DYNPRO_READ` | `RPY_DYNPRO_INSERT` | `RS_SCRP_DELETE` (no request parameter; recorded through the program's R3TR PROG entry, as in abapGit) |
| Native dynpro (`NAT_HEADER`/`NAT_FIELDS`, e.g. a splitter control) | `RS_SCREEN_LIST`, `RPY_DYNPRO_READ_NATIVE` | `RPY_DYNPRO_INSERT_NATIVE` | `RS_SCRP_DELETE` |
| GUI status, titles (CUA) | `RS_CUA_INTERNAL_FETCH` | `RS_CUA_INTERNAL_WRITE` (inactive) | |
| Text pool | `READ TEXTPOOL` | `INSERT TEXTPOOL … STATE 'I'` | |
| Report variants (`VARIS`, system variants `SAP&…`/`CUS&…`) | `RS_ALL_VARIANTS_4_1_REPORT`, `RS_VARIANT_VALUES_TECH_DAT_255`, `RS_VARIANT_CONTENTS_255`, `RS_GET_SCREENS_4_1_VARIANT`, `VARIT` (texts) | `RS_VARIANT_DELETE` + `RS_CREATE_VARIANT_255` + `RS_CHANGE_CREATED_VARIANT_255`, after the activation | `RS_VARIANT_DELETE`; with the program by `RS_DELETE_PROGRAM` |
| Translations: DOMA, DTEL short texts / fixed values / labels | `DDIF_DOMA_GET`, `DDIF_DTEL_GET` per language (languages from `DD01V`/`DD07V`/`DD04V`) | `RS_CORR_INSERT`, `DDIF_*_PUT` per language, `DDIF_*_ACTIVATE` | with the object |
| Translations: PROG text pool | `READ TEXTPOOL … LANGUAGE` (languages from `D010TINF`) | `RS_CORR_INSERT`, `INSERT TEXTPOOL … LANGUAGE … STATE 'A'` | with the object |
| Documentation (SE61: DOMA `DO`, DTEL `DE`/`DZ`, PROG `RE`) | `DOKIL`, `DOCU_READ` | `RS_CORR_INSERT`, `DOCU_UPDATE` (state `A`; identity set from the object, result checked through `DOKIL`) | `DOCU_DEL` |
| Activation of dynpros/CUA/text pool | | `RS_WORKING_OBJECTS_ACTIVATE` | |
| Transaction (TRAN) | `TSTC`, `TSTCC`, `TSTCT`, `TSTCP` | `RPY_TRANSACTION_DELETE` + `RPY_TRANSACTION_INSERT`; a failed insert re-inserts the previous version in its package | `RPY_TRANSACTION_DELETE` |
| System facts | `sy-uname`, `TRNSPACE`, `TADIR`/`TDEVC` (export: package of an object) | | |

Program and function-module **source** goes through the regular ADT source
endpoints, with the same locking and transport handling as `erpl-adt source
write`.

Payloads travel base64-encoded into the class; user data is never pasted into
ABAP source. The class body is a real ABAP file in this repository
(`src/abap/`), so it can be read and reviewed like any other code. A class
that fails to compile is never run (SAP would otherwise run its previous
version). If a run is interrupted (network, short dump), a leftover helper is
reported by name, with the hint to run `erpl-adt abapgit cleanup`. Every `abapgit` command quietly removes your own helpers
that are older than two hours; `erpl-adt abapgit cleanup [--older-than 60]`
does it on demand. Only genuine helper names (`ZCL_ERPLHLP_` + 8 hex digits)
owned by the logon user are touched, never a helper young enough to belong to
a run in progress.

## Translations and documentation

abapGit has two translation formats. By default (no `USE_LXE` in
`.abapgit.xml`) it writes the translations **into the object's XML file**,
next to the master language; that is the format `erpl-adt abapgit` reads and
writes. The newer LXE format (`USE_LXE` + `I18N_LANGUAGES`, separate
`<object>.i18n.<lang>.po` files) is not supported.

| Type | Translations in the XML | Documentation |
|---|---|---|
| DOMA | `I18N_LANGS`, `DD01_TEXTS` (short text), `DD07_TEXTS` (fixed values) | `LONGTEXTS` (`DO`) |
| DTEL | `I18N_LANGS`, `DD04_TEXTS` (short text, field labels) | `LONGTEXTS` (`DE`), `LONGTEXTS_DZ` (supplement) |
| PROG | `I18N_TPOOL` (title, text symbols, selection texts, headings) | `LONGTEXTS` (`RE`) |

The engine mirrors abapGit's serializers (`SERIALIZE_TEXTS`,
`ZCL_ABAPGIT_LONGTEXTS`): the same languages (all except the technical `1Q`
/ `2Q`), the same field clearing (`DOKSTATE`, the THEAD user/date fields),
the same sorting, so an export is byte-identical to what abapGit writes. A
fixed value without a translation is kept with empty texts, as in abapGit.

A deploy **manages exactly the languages its files name**: a translation or
documentation language that exists only in the system (e.g. added in SE63)
is neither compared nor written nor removed; abapGit leaves it alone too.
Transactions are the exception: their update is delete + insert, which drops
every short-text translation the system holds (no workbench API writes them
back). Such an update is refused with its own message ("would drop its
translations in the system (I18N_TPOOL (D))", exit 6); `--force` drops them
and notes it. An exported transaction file carries them (as abapGit's does)
and deploys unchanged; translations the system does not hold are refused
(exit 8).
An object whose files carry no translations therefore deploys exactly as
before. `export` writes every language the system has, as abapGit does.
Only what changed is written and reported (`DD07_TEXTS`, `I18N_TPOOL`,
`LONGTEXTS`, ...). DOMA/DTEL translations are written in the DDIC run (and
the object re-activated), text pool translations and documentation in the
write run, active right away as SE63/SE61 save them; no extra engine runs.
SAP records no last changer for translations, so a changed translation is
overwritten only when the system version is in the file's commit history or
was deployed from this clone (see *Own deploys*); a translation the system
does not have yet is never drift. The documentation's last changer is
`DOKHL-DOKLUSER`. `delete` removes the documentation with the object (as
abapGit does); translations go with the object. Because they go with it,
`delete` compares every language and **refuses** (exit 6) when the system
holds translation or documentation languages the files do not have (e.g.
`DD07_TEXTS (D)`, `LONGTEXTS (D, E)`), unless `--force`: pull them into git
with the printed export command first.

The documentation of a file is written as the documentation of the object
being deployed: `DOKIL-ID`/`OBJECT` and `THEAD-TDOBJECT`/`TDNAME`/`TDID` are
set from the object, never taken from the file, and a file whose
`LONGTEXTS` name another object (a file copied from `ZFOO`, or one naming
SAP's own `MATNR`) or a document type `DOCU_UPDATE` does not write (`TYP`
other than `E`/`T`) is refused before anything is written (exit 8).
`DOCU_UPDATE` has no exceptions; the engine checks the document's `DOKIL`
entry afterwards.

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
  `MASTER_LANGUAGE` (from `.abapgit.xml`), translations in the languages
  the files name (see *Translations and documentation*). The logon language
  (`--language`) does not change what is deployed or exported.
* **CI / pull-request checks** — `status` changes no object, but it does run a
  temporary helper class in `$TMP` (created, run, deleted; stale own helpers
  older than two hours are swept), so the CI user needs the same development
  authorization (`S_DEVELOP`) as for a deploy. Without `--transport`, objects
  in transportable packages get a note ("the deploy needs --transport")
  instead of a refusal; `--strict` makes it a refusal (exit 9). `status`
  exits 0 when it could run: drift, a missing `--transport`, namespace
  refusals and parts a deploy could not write are reported (text and JSON),
  not fatal. With `--strict` any refusal or failure exits with the most
  severe code (e.g. 6 for drift) — use that to block a merge. A system that
  cannot be read (connection, helper class) and a file that cannot be loaded
  or parsed always exit non-zero. `--json` has `command` (`status` for
  status, with `dry_run: true`) and gives one entry per object with
  `action` (`created`, `updated`, `unchanged`, `deleted`, `missing`,
  `exported`, `failed`, `refused`, `skipped`), the written `parts`, SAP's
  `message`, `notes`, and for failures `category` and `exit_code`; the
  top-level `exit_code` is the command's exit code.
* **Failures in dependency order** — when an object fails or is refused,
  later objects whose files name it (a data element's domain, a transaction's
  program) are reported as `skipped: dependency DOMA X failed` and not
  attempted; `status` shows the same.
* **Progress** — on a terminal (and without `--json`) deploy and delete print
  one line per object to stderr: `[3/8] updating PROG ZERPL_SO_DISPLAY`.
* **Export** — a refresh writes every object back into the folder it was
  scanned from; `--object` writes into the folder of the object's package
  (TADIR, mapped with the repository's folder logic) and refuses objects of
  packages outside the repository's root package. Files of parts the system no
  longer has (e.g. a screen deleted in SE51) are removed and reported as
  `removed <file>`, so git shows the deletion.

## MCP tools

`erpl-adt mcp` serves the four commands as tools for AI agents:

| Tool | Command | Classification |
|---|---|---|
| `abapgit_status` | `abapgit status` | read-only |
| `abapgit_deploy` | `abapgit deploy` | destructive |
| `abapgit_export` | `abapgit export` | destructive (overwrites and removes local files) |
| `abapgit_delete` | `abapgit delete` | destructive |

Arguments mirror the flags: `path` and `package` (required), `transport`,
`dry_run`, `force`, `strict`, `objects` (export, `["PROG:ZDEMO", ...]`) and
`master_language` (export into a new repository). An unknown argument (a
mistyped `dryrun`) is refused, like an unknown flag. The result —
`structuredContent` and the text block — is exactly the `--json` report of the
CLI; `isError` is set when its `exit_code` is not 0.

The tools only read and write files below one folder, the **abapGit root**:
`--abapgit-root <dir>`, else `mcp.abapgit_root` in the `-c` YAML file
(relative to that file), else the server's working directory.

```yaml
mcp:
  abapgit_root: ./repos
```

```json
{"name": "abapgit_status", "arguments": {"path": "erpl_demo/src/zerpl_d_so_status.doma.xml", "package": "$TMP"}}
```

`path` is resolved against the root (`weakly_canonical`, so `..` and existing
symlinks are resolved first). A path that ends up outside the root — `..`,
an absolute path elsewhere, a symlink pointing out — is refused before
anything runs (category `authorization`, exit code 1), as is a repository
containing a symlink that leads outside the root (a deploy would read through
it, an export would write through it). Absolute paths inside the root are
accepted. The search for `.abapgit.xml` stops at the root. A `STARTING_FOLDER`
with a `..` segment is refused for every command (CLI too), and an export
never writes or removes files in a folder outside the repository, whatever
the layout says. The deploy record stays inside the root or in the server's
cache (see *Own deploys*).

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
| 99 | usage error (unknown flag, invalid argument), a local file that cannot be read, parsed or written, or internal error |

Failures are classified by evidence, never by (translated) message text: the
helper's own verdict from the function module's exception, else SAP's T100
message id (e.g. `MC 601`, `EU 510`, `DS 068` → 6; other `TK`/`TR` → 9).
When several objects fail, the most severe category decides the exit code:
1 > 99 > 10 > 9 > 6 > 5 > 8 > 2 — environment and guard-rail problems first,
then problems that need a human (CTS, someone else's lock or change), then
problems in the code being deployed.

## Limitations

* Line endings: a CRLF working copy (Windows with `core.autocrlf=true`) is
  read as LF. `deploy`, `status` and `delete` turn every CRLF pair in the
  object files and in `.abapgit.xml` into LF before they compare, plan and
  record, so such a checkout reads as unchanged. Only CRLF pairs change: a
  lone CR in ABAP source stays content (in XML, a CR that is content is
  written as `&#13;` and kept). `export` writes LF, as abapGit does.
* Supported types: DOMA, DTEL, FUGR (function modules), PROG, TRAN. Classes,
  interfaces, tables (TABL) and packages (DEVC) are not created by `abapgit
  deploy`; create packages with `erpl-adt object create --type DEVC/K`, and
  deploy classes with `erpl-adt source write`.
* Translations: only abapGit's in-XML format, for DOMA, DTEL and PROG text
  pools. Not supported: the LXE format (`*.i18n.<lang>.po`), TRAN short-text
  translations (abapGit writes `TSTCT` directly, which erpl-adt does not do;
  they are refused as before), FUGR text pool translations and function
  module documentation, dynpro and GUI status translations (abapGit carries
  those only in the LXE format). Removing a translation language from the
  files does not remove it from the system (as in abapGit).
* Report variants: only system variants (`SAP&…`, `CUS&…`; client 000,
  transported with the program) — abapGit's `VARIS` holds no others, and a
  user variant in the file is refused (exit 8). Variants are written after the
  program's activation (SAP checks them against the generated selection
  screen) and have no inactive version. Where abapGit unprotects a protected
  variant with a direct `UPDATE VARID` before replacing it, erpl-adt uses
  `RS_VARIANT_DELETE`'s `suppress_protected_check`; drift protection has
  refused a variant someone else changed before that. Variant texts are
  written in the master language only.
* Native dynpros: a screen holding an element the external dynpro format
  cannot describe (`D021S-FILL` 'X', e.g. a dynpro splitter control) is kept
  in SAP's internal format, like abapGit (`NAT_HEADER`, `NAT_FIELDS`). It is
  written with `RPY_DYNPRO_INSERT_NATIVE` (locks the screen, records it on
  the request, registers it inactive; the activation generates it). Unlike
  abapGit, an existing screen is overwritten (`suppress_exist_checks`;
  abapGit's call answers `ALREADY_EXISTS`, which it ignores, so abapGit only
  ever creates such screens). The function module writes the screen source
  directly, so - unlike other screens - there is no previous version to
  restore: a screen SAP cannot generate stays inactive and the object fails
  (exit 5). Field texts of a native screen (`NAT_TEXTS`, table `D021T`) are
  refused (exit 8): abapGit inserts them into `D021T` directly and no
  workbench API writes them. A plain splitter screen has none.
* Transaction authorization-object checks (`AUTHORIZATIONS`, table `TSTCA`
  with the `CINFO` bit x'04') are refused (exit 8). `RPY_TRANSACTION_INSERT`
  has no parameter for them; classic SE93 (form in `LSEUKF01`) and abapGit's
  `SAVE_AUTHORIZATIONS` both `DELETE`/`INSERT TSTCA` and `UPDATE TSTC`
  directly, and the only class that writes them (`CL_TRAN_WBI_P`, the
  persistence of the new transaction editor, which SAP enables only in SAP's
  own development systems) does the same, without lock or transport. Maintain
  the check in SE93. The same holds for report transactions with a variant
  (`CINFO` x'10') and translated short texts. Because an update is delete +
  insert, an existing transaction whose system version has such a check, a
  variant flag or an OO type (x'08') is refused as well (exit 8, also with
  `--force`): the re-insert would silently drop it. On A4H no such
  transaction can be set up without direct table writes, so this refusal is
  unit-tested only.
* The drift check needs `git` on the PATH to consult file history (the last
  20 commits of each file); without it, only parts provably last changed by
  you count as safe — screens and transactions then always need `--force`.
* Transport recording (`--transport`) uses the function modules' own request
  parameters and `RS_CORR_INSERT`, but it has not been verified live: the
  A4H trial's client settings were left unchanged (owner decision), so no
  transportable package with a request was available for tests. Screen
  deletions follow abapGit exactly: `RS_SCRP_DELETE` has no request
  parameter, and the deletion is expected on the request through the R3TR
  PROG entry `RS_CORR_INSERT` makes first (abapGit's
  `ZCL_ABAPGIT_OBJECTS_PROGRAM->DESERIALIZE_DYNPROS` relies on the same) —
  unverified as well.
* A transaction update is delete + insert (there is no update API).
  `RPY_TRANSACTION_INSERT` checks little — measured on A4H, it accepts a
  `PGMNA` that does not exist — but when it fails (e.g. its `RS_CORR_INSERT`
  rejects the package), the previous version (`TSTC`/`TSTCC`/`TSTCT`/`TSTCP`
  as read before the delete) is inserted again into its package and the
  object fails with "its previous version was restored". Short-text
  translations of the previous version are not restored (no workbench API
  writes them; see above).
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
  screen) is saved by `RPY_DYNPRO_INSERT` before it is generated, as in SE51.
  The deploy therefore reads the screen first (`RPY_DYNPRO_READ`) and, when
  SAP cannot generate the new version, writes the previous one back — or
  deletes the screen again if it did not exist. The deploy fails with SAP's
  message (exit 5) plus "Screen NNNN was restored to its previous version";
  the system is as before, and the fixed file deploys without `--force`.
