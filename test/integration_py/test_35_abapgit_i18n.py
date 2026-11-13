"""Integration tests for `erpl-adt abapgit` translations and documentation.

abapGit's default translation format (no USE_LXE / I18N_LANGUAGES in
.abapgit.xml) keeps the translations inside the object's XML file, next to
the master language:

  DOMA  I18N_LANGS + DD01_TEXTS (short text) + DD07_TEXTS (fixed values)
  DTEL  I18N_LANGS + DD04_TEXTS (short text and field labels)
  PROG  I18N_TPOOL (text pool: title, text symbols, selection texts, headings)

and the documentation (SE61 long texts) as LONGTEXTS (DOKIL + THEAD + TLINE
lines, every language). The erpl_demo fixtures carry German translations and
documentation; these tests deploy them as ZERPL_I18N_* copies (so they never
race with other tests on the erpl_demo objects) and check the result
independently of erpl-adt's engine, over ADT REST: element info and the
repository search answer in the logon language (Accept-Language de), the
SAPscript documentation endpoint renders SE61 documents, and the data
preview reads the DDIC text tables (the domain editor endpoint shows the
original language only). ADT shows text pools in the original language only,
so a console class reads the German text pool with READ TEXTPOOL.
`erpl-adt --language DE` reads German too.
"""

import re
import shutil
import subprocess
from pathlib import Path

import pytest

FIXTURES = Path(__file__).parent / "fixtures" / "abapgit" / "erpl_demo"
SRC = FIXTURES / "src"
PACKAGE = "$TMP"

# erpl_demo object -> its ZERPL_I18N_* copy
RENAME = {
    "ZERPL_D_SO_STATUS": "ZERPL_I18N_D_SO_STATUS",
    "ZERPL_E_SO_STATUS": "ZERPL_I18N_E_SO_STATUS",
    "ZERPL_PO_REPORT": "ZERPL_I18N_PO_REPORT",
    "ZERPL_SO_DISPLAY": "ZERPL_I18N_SO_DISPLAY",
}
DOMA = RENAME["ZERPL_D_SO_STATUS"]
DTEL = RENAME["ZERPL_E_SO_STATUS"]
REPORT = RENAME["ZERPL_PO_REPORT"]
DISPLAY = RENAME["ZERPL_SO_DISPLAY"]
TPOOL_CHECK = "ZERPL_I18N_TPOOL_CHECK"


def _entry(result, obj_type, name):
    for obj in result["objects"]:
        if obj["type"] == obj_type and obj["name"] == name:
            return obj
    raise AssertionError(f"{obj_type} {name} not in result: {result['objects']}")


def _renamed(data: bytes) -> bytes:
    for old, new in RENAME.items():
        data = re.sub(rb"\b" + old.encode() + rb"\b", new.encode(), data)
        data = re.sub(rb"\b" + old.lower().encode() + rb"\b", new.lower().encode(), data)
    return data


def _git(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=erpl-adt test",
                    "-c", "user.email=test@erpl.invalid", *args],
                   check=True, capture_output=True)


def _make_repo(root: Path) -> Path:
    """A git repository with the ZERPL_I18N_* copies of the translated objects.

    Committed, as in a real repository: SAP records no last changer for
    translations, so a deploy that changes one is only allowed when the
    system version is in the file's git history (or was deployed from here)."""
    repo = root / "repo"
    (repo / "src").mkdir(parents=True)
    shutil.copy(FIXTURES / ".abapgit.xml", repo / ".abapgit.xml")
    for old in RENAME:
        for f in sorted(SRC.glob(old.lower() + ".*")):
            target = repo / "src" / _renamed(f.name.encode()).decode()
            target.write_bytes(_renamed(f.read_bytes()))
    _git(repo, "init", "-q")
    _git(repo, "add", ".")
    _git(repo, "commit", "-qm", "fixture")
    return repo


def _commit(repo, message="change"):
    _git(repo, "add", ".")
    _git(repo, "commit", "-qm", message)


class _AdtReader:
    """Read-only ADT REST access with the standard library, used to verify
    deployed objects independently of the erpl-adt code under test."""

    def __init__(self, cfg):
        import base64
        import http.cookiejar
        import urllib.request
        self.base = f"http://{cfg['host']}:{cfg['port']}"
        token = base64.b64encode(f"{cfg['user']}:{cfg['password']}".encode()).decode()
        self.headers = {"Authorization": f"Basic {token}", "sap-client": cfg["client"],
                        "Accept": "*/*"}
        # One logon for the data preview (CSRF token + session cookie),
        # logged off at the end of the module.
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.csrf = None

    def request(self, path, headers=None, data=None, opener=None):
        import urllib.error
        import urllib.request
        req = urllib.request.Request(self.base + path, data=data,
                                     headers={**self.headers, **(headers or {})})
        try:
            with (opener or urllib.request.build_opener()).open(req, timeout=120) as resp:
                return resp.status, resp.read().decode("utf-8", "replace"), resp.headers
        except urllib.error.HTTPError as err:
            return err.code, err.read().decode("utf-8", "replace"), err.headers

    def get(self, path, headers=None):
        status, body, _ = self.request(path, headers)
        return status, body

    def sql(self, query):
        """Rows of an ABAP SQL query (ADT data preview, read-only)."""
        if self.csrf is None:
            _, _, hdrs = self.request("/sap/bc/adt/discovery", {"x-csrf-token": "fetch"},
                                      opener=self.opener)
            self.csrf = hdrs.get("x-csrf-token")
        status, body, _ = self.request(
            "/sap/bc/adt/datapreview/freestyle?rowNumber=100",
            {"x-csrf-token": self.csrf, "Content-Type": "text/plain",
             "Accept": "application/xml, application/vnd.sap.adt.datapreview.table.v1+xml"},
            data=query.encode(), opener=self.opener)
        assert status == 200, body[:800]
        columns = [re.findall(r"<dataPreview:data>([^<]*)</dataPreview:data>", c)
                   for c in body.split("<dataPreview:columns>")[1:]]
        return [tuple(c[i] for c in columns) for i in range(len(columns[0]))] if columns else []

    def logoff(self):
        if self.csrf is not None:
            self.request("/sap/bc/adt/discovery?sap-sessioncmd=logoff", opener=self.opener)

    def element_info(self, name, lang):
        status, body = self.get(f"/sap/bc/adt/ddic/elementinfo?path={name.lower()}",
                                {"Accept-Language": lang})
        assert status == 200, body[:500]
        return body

    def documentation(self, doc_id, name, lang):
        """An SE61 document rendered as HTML ("" when there is none)."""
        status, body = self.get(
            f"/sap/bc/adt/documentation/sapscript/documents/{doc_id.lower()}/{name.lower()}"
            f"?language={lang.lower()}&mode=adteleminfo")
        return body if status == 200 else ""

    def text_elements(self, program, kind, lang):
        status, body = self.get(
            f"/sap/bc/adt/textelements/programs/{program.lower()}/source/{kind}",
            {"Accept": f"application/vnd.sap.adt.textelements.{kind}.v1",
             "Accept-Language": lang})
        assert status == 200, body[:500]
        return body.replace("\r\n", "\n")

    def descriptions(self, pattern, lang):
        status, body = self.get(
            "/sap/bc/adt/repository/informationsystem/search?operation=quickSearch"
            f"&query={pattern}&maxResults=20", {"Accept-Language": lang})
        assert status == 200, body[:500]
        out = {}
        for attrs in re.findall(r"<adtcore:objectReference ([^>]*)>", body):
            name = re.search(r'adtcore:name="([^"]*)"', attrs)
            desc = re.search(r'adtcore:description="([^"]*)"', attrs)
            if name:
                out[name.group(1)] = desc.group(1) if desc else ""
        return out

    def fixed_values(self, domain, lang):
        rows = self.sql(f"SELECT valpos, ddtext FROM dd07t WHERE domname = '{domain}' "
                        f"AND ddlanguage = '{lang}' AND as4local = 'A'")
        return dict(sorted(rows))

    def domain_text(self, domain, lang):
        rows = self.sql(f"SELECT ddtext FROM dd01t WHERE domname = '{domain}' "
                        f"AND ddlanguage = '{lang}' AND as4local = 'A'")
        return rows[0][0] if rows else None


@pytest.fixture(scope="module")
def adt(sap_config):
    reader = _AdtReader(sap_config)
    yield reader
    reader.logoff()


@pytest.fixture(scope="module")
def repo(tmp_path_factory):
    return _make_repo(tmp_path_factory.mktemp("i18n"))


TPOOL_CHECK_SOURCE = """CLASS @CLASS@ DEFINITION PUBLIC FINAL CREATE PUBLIC.
  PUBLIC SECTION.
    INTERFACES if_oo_adt_classrun.
ENDCLASS.

CLASS @CLASS@ IMPLEMENTATION.
  METHOD if_oo_adt_classrun~main.
    DATA tpool TYPE textpool_table.
    READ TEXTPOOL '@REPORT@' INTO tpool LANGUAGE 'D'.
    LOOP AT tpool INTO DATA(entry).
      out->write( |TPOOL { entry-id } { entry-key }={ entry-entry }| ).
    ENDLOOP.
  ENDMETHOD.
ENDCLASS.
"""


@pytest.fixture(scope="module")
def german_tpool(cli, tmp_path_factory):
    """The German text pool of the report, read with READ TEXTPOOL by a console class."""
    uri = f"/sap/bc/adt/oo/classes/{TPOOL_CHECK.lower()}"
    cli.run("object", "delete", uri)
    cli.run_ok("object", "create", "--type", "CLAS/OC", "--name", TPOOL_CHECK,
               "--package", PACKAGE, "--description", "erpl-adt i18n test: read text pool")
    source = tmp_path_factory.mktemp("tpool") / "check.abap"
    source.write_text(TPOOL_CHECK_SOURCE.replace("@CLASS@", TPOOL_CHECK.lower())
                      .replace("@REPORT@", REPORT))
    written = cli.run("source", "write", uri + "/source/main", "--file", str(source),
                      "--activate")
    assert written.returncode == 0, written.stderr

    def read():
        out = cli.run_ok("object", "run", TPOOL_CHECK)["output"]
        entries = {}
        for line in out.splitlines():
            m = re.match(r"TPOOL (\w) (\S*) ?=(.*)$", line.strip())
            if m:
                entries[(m.group(1), m.group(2))] = m.group(3).rstrip()
        return entries

    yield read
    cli.run("object", "delete", uri)


def _deploy(cli, path, *flags):
    return cli.run_ok("abapgit", "deploy", str(path), "--package", PACKAGE, *flags)


GERMAN_VALUES = {"0001": "Neu", "0002": "In Bearbeitung", "0003": "Abgeschlossen",
                 "0004": "Storniert"}


@pytest.mark.abapgit_deploy
class TestTranslationsAndDocumentation:
    """German translations and SE61 documentation of the erpl_demo objects."""

    def test_deploy_creates_objects_with_german_texts(self, cli, adt, repo):
        cli.run("abapgit", "delete", str(repo), "--package", PACKAGE, "--force")

        result = _deploy(cli, repo)

        for obj_type, name in [("DOMA", DOMA), ("DTEL", DTEL), ("PROG", REPORT),
                               ("PROG", DISPLAY)]:
            assert _entry(result, obj_type, name)["action"] == "created", result
        doma = _entry(result, "DOMA", DOMA)["parts"]
        assert doma == ["DD01V", "DD07V_TAB", "DD01_TEXTS", "DD07_TEXTS"], doma
        dtel = _entry(result, "DTEL", DTEL)["parts"]
        assert dtel == ["DD04V", "DD04_TEXTS", "LONGTEXTS"], dtel
        report = _entry(result, "PROG", REPORT)["parts"]
        assert report[-2:] == ["I18N_TPOOL", "LONGTEXTS"], report
        assert _entry(result, "PROG", DISPLAY)["parts"][-1] == "I18N_TPOOL"

        # Short texts in the logon language (repository search).
        german = adt.descriptions("ZERPL_I18N*", "de")
        assert german[DOMA] == "Lebenszyklusstatus des Kundenauftrags", german
        assert german[DTEL] == "Status des Kundenauftrags"
        assert german[REPORT] == "Bestellungen nach Lieferant"
        assert german[DISPLAY] == "Kundenauftrag anzeigen"
        english = adt.descriptions("ZERPL_I18N*", "en")
        assert english[DOMA] == "Sales order lifecycle status", english
        assert english[DISPLAY] == "Display Sales Order"

        # Fixed values, both languages.
        assert adt.fixed_values(DOMA, "D") == GERMAN_VALUES
        assert adt.fixed_values(DOMA, "E")["0004"] == "Cancelled"

        # Field labels in the logon language (F2 element info).
        info = adt.element_info(DTEL, "de")
        assert "Kundenauftragsstatus" in info and "Auftragsstatus" in info, info[:1500]
        assert "Sales Order Status" in adt.element_info(DTEL, "en")

        # The master language text pool is unchanged.
        assert "Maximum number of items" in adt.text_elements(REPORT, "selections", "en")

    def test_german_text_pool_is_readable(self, german_tpool):
        tpool = german_tpool()
        assert tpool[("R", "")] == "Bestellungen nach Lieferant", tpool
        assert tpool[("I", "001")] == "Keine Bestellungen entsprechen der Selektion"
        # Selection texts keep their 8-character prefix (abapGit's SPLIT).
        assert tpool[("S", "P_MAX")].strip() == "Max. Anzahl Positionen"
        assert tpool[("S", "S_BP_ID")].strip() == "Lieferer"
        assert tpool[("S", "P_STATUS")].startswith("D"), tpool

    def test_documentation_is_rendered_by_sap(self, adt):
        assert "Lifecycle status of a sales order in the EPM demo model." in \
            adt.documentation("DE", DTEL, "E")
        assert "Lebenszyklusstatus eines Kundenauftrags im EPM-Demomodell." in \
            adt.documentation("DE", DTEL, "D")
        assert "Lists the purchase orders of the EPM demo model by supplier." in \
            adt.documentation("RE", REPORT, "E")

    def test_logon_language_de_reads_german(self, cli):
        result = cli.run_ok("search", "ZERPL_I18N_*", "--max=20",
                            extra_flags=["--language", "DE"])
        text = str(result)
        assert "Lebenszyklusstatus des Kundenauftrags" in text, text
        assert "Bestellungen nach Lieferant" in text, text

    def test_redeploy_unchanged_changes_nothing(self, cli, repo):
        result = _deploy(cli, repo)
        for obj in result["objects"]:
            assert obj["action"] == "unchanged", obj

        # The logon language does not matter: the files hold every language.
        result = cli.run_ok("abapgit", "deploy", str(repo), "--package", PACKAGE,
                            extra_flags=["--language", "DE"])
        for obj in result["objects"]:
            assert obj["action"] == "unchanged", obj

    def test_export_matches_files_byte_for_byte(self, cli, repo, tmp_path):
        fixture = {p.name: p.read_bytes() for p in (repo / "src").iterdir()}
        cli.run_ok("abapgit", "export", str(repo), "--package", PACKAGE,
                   extra_flags=["--language", "DE"])
        for name, data in fixture.items():
            assert (repo / "src" / name).read_bytes() == data, name

        out = tmp_path / "out"
        cli.run_ok("abapgit", "export", str(out), "--package", PACKAGE,
                   "--object", f"DTEL:{DTEL},PROG:{REPORT}")
        for name in (f"{DTEL.lower()}.dtel.xml", f"{REPORT.lower()}.prog.xml"):
            assert (out / "src" / name).read_bytes() == fixture[name], name

    def test_changed_translation_updates_only_the_translation(self, cli, adt, repo,
                                                              german_tpool):
        doma_file = repo / "src" / f"{DOMA.lower()}.doma.xml"
        doma_file.write_bytes(doma_file.read_bytes().replace(
            b"<DDTEXT>Storniert</DDTEXT>", b"<DDTEXT>Annulliert</DDTEXT>"))
        dtel_file = repo / "src" / f"{DTEL.lower()}.dtel.xml"
        dtel_file.write_bytes(dtel_file.read_bytes().replace(
            b"<SCRTEXT_M>Auftragsstatus</SCRTEXT_M>", b"<SCRTEXT_M>Auftragsstand</SCRTEXT_M>"))
        prog_file = repo / "src" / f"{REPORT.lower()}.prog.xml"
        prog_file.write_bytes(prog_file.read_bytes().replace(
            b"<ENTRY>Selektion</ENTRY>", b"<ENTRY>Auswahl</ENTRY>"))

        result = _deploy(cli, repo)

        assert _entry(result, "DOMA", DOMA)["action"] == "updated"
        assert _entry(result, "DOMA", DOMA)["parts"] == ["DD07_TEXTS"]
        assert _entry(result, "DTEL", DTEL)["parts"] == ["DD04_TEXTS"]
        assert _entry(result, "PROG", REPORT)["parts"] == ["I18N_TPOOL"]
        assert _entry(result, "PROG", DISPLAY)["action"] == "unchanged"
        assert adt.fixed_values(DOMA, "D")["0004"] == "Annulliert"
        assert adt.fixed_values(DOMA, "E")["0004"] == "Cancelled"
        assert "Auftragsstand" in adt.element_info(DTEL, "de")
        assert german_tpool()[("I", "B01")] == "Auswahl"
        assert "Selection" in adt.text_elements(REPORT, "symbols", "en")
        _commit(repo, "German texts reworded")

        result = _deploy(cli, repo)
        for obj in result["objects"]:
            assert obj["action"] == "unchanged", obj

    def test_changed_documentation_updates_only_the_documentation(self, cli, adt, repo):
        dtel_file = repo / "src" / f"{DTEL.lower()}.dtel.xml"
        dtel_file.write_bytes(dtel_file.read_bytes().replace(
            b"Anzeige des Kundenauftrags (Transaktion ZERPL_VA03).",
            b"Kundenauftrag anzeigen (Transaktion ZERPL_VA03)."))

        result = _deploy(cli, repo)

        entry = _entry(result, "DTEL", DTEL)
        assert entry["action"] == "updated"
        assert entry["parts"] == ["LONGTEXTS"]
        assert "Kundenauftrag anzeigen (Transaktion ZERPL_VA03)." in \
            adt.documentation("DE", DTEL, "D")
        assert "Shown on the sales order display (transaction ZERPL_VA03)." in \
            adt.documentation("DE", DTEL, "E")
        _commit(repo, "docu reworded")

    def test_files_without_translations_leave_them_alone(self, cli, adt, repo, tmp_path):
        """A file without I18N sections deploys only the master language, as
        before: the German texts in the system are neither compared nor removed."""
        plain = tmp_path / f"{DOMA.lower()}.doma.xml"
        data = (repo / "src" / plain.name).read_bytes()
        plain.write_bytes(re.sub(rb"   <I18N_LANGS>.*</DD07_TEXTS>\n", b"", data, flags=re.S))
        assert b"I18N_LANGS" not in plain.read_bytes()

        result = _deploy(cli, plain)

        assert _entry(result, "DOMA", DOMA)["action"] == "unchanged"
        assert adt.fixed_values(DOMA, "D")["0004"] == "Annulliert"

    def test_delete_removes_objects_and_documentation(self, cli, adt, repo):
        result = cli.run_ok("abapgit", "delete", str(repo), "--package", PACKAGE)
        for obj in result["objects"]:
            assert obj["action"] == "deleted", obj
        assert adt.fixed_values(DOMA, "D") == {}
        left = set(adt.descriptions("ZERPL_I18N*", "de")) & {DOMA, DTEL, REPORT, DISPLAY}
        assert left == set(), left
        assert adt.sql(f"SELECT id, object, langu FROM dokil WHERE object IN "
                       f"('{DTEL}', '{REPORT}', '{DOMA}')") == []
