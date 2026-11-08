# Classic GUI objects from git: `erpl-adt abapgit`

`erpl-adt abapgit` deploys classic SAP GUI objects — dynpros, GUI status and
titles, transaction codes, domains, data elements, text pools and function
modules — from files in **abapGit's serialization format**, and exports them
back into exactly the same bytes. A repository can be edited in git, linted
with abaplint, pulled with abapGit inside SAP, or deployed headless with
erpl-adt from CI — all on the same files.

```bash
# What would change? (writes nothing — use it in pull-request checks)
erpl-adt abapgit status ./erpl-demo --package ZERPL_DEMO --transport A4HK900146

# Create/update everything in dependency order, then activate
erpl-adt abapgit deploy ./erpl-demo --package ZERPL_DEMO --transport A4HK900146

# One object only (any of its files)
erpl-adt abapgit deploy ./erpl-demo/src/zerpl_so_display.prog.xml --package '$TMP'

# Pull changes made in SE51/SE41/SE93 back into git (refreshes the files in <dir>)
erpl-adt abapgit export ./erpl-demo --package ZERPL_DEMO

# Start a repository from objects that already exist
erpl-adt abapgit export ./new-repo --package '$TMP' --object PROG:ZERPL_SO_DISPLAY,TRAN:ZERPL_VA03

# Remove what the files describe (reverse dependency order)
erpl-adt abapgit delete ./erpl-demo --package '$TMP'
```

`<path>` is either the root of an abapGit repository (the folder holding
`.abapgit.xml`) or a single object file. All commands accept `--json`.

## What a deploy does

1. **Scan** the repository like abapGit: `.abapgit.xml` (starting folder,
   PREFIX/FULL folder logic, ignore list, master language), file names
   (`zfoo.prog.xml`, `zfoo.prog.screen_0100.abap`, `#ns#obj.tran.xml`, …),
   sub-folders mapped to sub-packages.
2. **Guard rails**, before anything touches the system:
   * customer objects only — `Y*`, `Z*`, or a `/NAMESPACE/` that is
     changeable in this system (SE03, `TRNSPACE`);
   * no SAP packages; a transportable package needs `--transport`;
   * unsupported object types and files are listed, never silently dropped.
3. **Read** the current state of every object in one round trip and render it
   exactly as abapGit would.
4. **Compare** byte for byte. Unchanged objects are left alone — re-running a
   deploy is safe and fast. Changed objects are updated in place, part by part
   (`source`, `screen 0200`, `CUA`, `TPOOL`, …).
5. **Drift protection**: an object is only overwritten if its current system
   version came from git (it equals an earlier committed version of its
   files) or was last changed by you. Otherwise someone changed it in the
   system after the last deploy, and the object is refused (exit code 6) with
   who changed it and when. Pull it into git with `abapgit export`, or
   overwrite it with `--force`.
6. **Activate** through the standard workbench activation; activation errors
   are reported per object.

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
| Domain (DOMA) | `DDIF_DOMA_GET` | `RS_CORR_INSERT`, `DDIF_DOMA_PUT`, `DDIF_DOMA_ACTIVATE` | `RS_DD_DELETE_OBJ` |
| Dynpro | `RS_SCREEN_LIST`, `RPY_DYNPRO_READ` | `RPY_DYNPRO_INSERT` | `RS_SCRP_DELETE` |
| GUI status, titles (CUA) | `RS_CUA_INTERNAL_FETCH` | `RS_CUA_INTERNAL_WRITE` (inactive) | |
| Text pool | `READ TEXTPOOL` | `INSERT TEXTPOOL … STATE 'I'` | |
| Activation of dynpros/CUA/text pool | | `RS_WORKING_OBJECTS_ACTIVATE` | |
| Transaction (TRAN) | `TSTC`, `TSTCC`, `TSTCT`, `TSTCP` | `RPY_TRANSACTION_INSERT` | `RPY_TRANSACTION_DELETE` |
| System facts | `sy-uname`, `TRNSPACE` | | |

Program and function-module **source** goes through the regular ADT source
endpoints, with the same locking and transport handling as `erpl-adt source
write`.

Payloads travel base64-encoded into the class; user data is never pasted into
ABAP source. The class body is a real ABAP file in this repository
(`src/abap/`), so it can be read and reviewed like any other code. A class
that fails to compile is never run (SAP would otherwise run its previous
version). If a run is interrupted (network, short dump), a leftover helper is
reported by name; `erpl-adt search 'ZCL_ERPLHLP*'` lists them and
`erpl-adt object delete /sap/bc/adt/oo/classes/<name>` removes one.

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
* **CI** — `status` is read-only and returns a non-zero exit code if an object
  would be refused; `deploy --json` gives one entry per object with `action`
  (`created`, `updated`, `unchanged`, `failed`, `refused`), the written
  `parts`, and SAP's `message`.

## Exit codes

`0` everything deployed / unchanged · `1` authorization or namespace refused ·
`2` object or path not found · `5` SAP rejected an object (activation, check) ·
`6` lock conflict or drift · `9` transport problem (missing or wrong request) ·
`99` usage error.

## Limitations

See the pull request for the current list. Known so far:

* Supported types: DOMA, DTEL, FUGR (function modules), PROG, TRAN. Classes,
  interfaces, tables (TABL) and packages (DEVC) are not created by `abapgit
  deploy`; create packages with `erpl-adt object create --type DEVC/K`, and
  deploy classes with `erpl-adt source write`.
* Translations (I18N/LXE) and long-text documentation (DOCU) are not
  deployed; only the master language.
* Native dynpros (splitter screens stored as `NAT_HEADER`) and report variants
  (`VARIS`) are reported as unsupported parts.
* The drift check needs `git` on the PATH to consult file history; without it,
  only "last changed by you" counts as safe.
