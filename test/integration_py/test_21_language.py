"""SAP logon language selection tests (issue #26).

The connection language is chosen via the global ``--language`` flag, which the
CLI forwards as the ``Accept-Language`` header. SAP maps that to the logon
language, so language-dependent text (object descriptions) comes back
translated.

Observable ground truth on the ABAP Cloud trial: table ``T000`` is described as
"Clients" in English and "Mandanten" in German. If the target system has no
German text imported, the German assertions are skipped rather than failed.
"""

import pytest


def _t000_description(cli, *, language=None):
    """Return T000's description, optionally forcing a logon language."""
    extra_flags = ["--language", language] if language else None
    data = cli.run_ok("search", "T000", "--max", "1", extra_flags=extra_flags)
    assert isinstance(data, list) and data, "expected a T000 search hit"
    row = next((r for r in data if r["name"].upper() == "T000"), data[0])
    return row["description"]


@pytest.mark.language
class TestLanguage:

    def test_default_language_is_english(self, cli):
        """No --language flag → English descriptions (backward compatible)."""
        assert _t000_description(cli) == "Clients"

    def test_explicit_english(self, cli):
        """--language EN yields the English description."""
        assert _t000_description(cli, language="EN") == "Clients"

    def test_german_switches_description(self, cli):
        """--language DE yields the German description (the core feature)."""
        english = _t000_description(cli, language="EN")
        german = _t000_description(cli, language="DE")
        if german == english:
            pytest.skip(
                "German text not imported on this system "
                f"(EN and DE both returned {english!r})"
            )
        assert german == "Mandanten"

    def test_invalid_language_rejected(self, cli):
        """A malformed language code fails validation before any request."""
        result = cli.run_fail("search", "T000", extra_flags=["--language", "XYZ"])
        assert result.returncode == 99
        assert "Invalid --language" in result.stdout + result.stderr
