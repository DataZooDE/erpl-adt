"""BW create + versioned read tests (issue #41).

These cover content negotiation on the BW modeling routes: a request that does
not name the object type's media type in Accept is answered with HTTP 415
("Requested content type */* does not match back-end content type ..."), which
made every form of `bw create` fail.
"""

import json
import uuid

import pytest


def _unique_name(prefix="ZTST"):
    # ADSO names are limited to 9 characters ("Object name ... must be between
    # 3 and 9 characters long").
    return (prefix + uuid.uuid4().hex[:5]).upper()


@pytest.fixture
def adso_cleanup(cli, tmp_path):
    """Delete every ADSO a test registers, whatever the test's outcome.

    Lock and delete must share one stateful session, hence --session-file.
    """
    created = []
    yield created
    for name in created:
        session_file = str(tmp_path / f"cleanup_{name}.json")
        lock = cli.run("bw", "lock", "ADSO", name, session_file=session_file)
        if lock.returncode != 0:
            continue
        try:
            handle = json.loads(lock.stdout.strip()).get("lock_handle", "")
        except (ValueError, AttributeError):
            continue
        if handle:
            cli.run("bw", "delete", "ADSO", name, "--lock-handle", handle,
                    session_file=session_file)


@pytest.mark.bw
class TestBwCreate:

    def test_create_adso_in_package(self, cli, bw_has_adso, adso_cleanup):
        """bw create ADSO <name> --package $TMP succeeds (was HTTP 415)."""
        name = _unique_name()
        result = cli.run("bw", "create", "ADSO", name, "--package", "$TMP")
        assert result.returncode == 0, result.stderr
        assert "415" not in result.stderr
        adso_cleanup.append(name)

        # A newly created object exists only in its inactive (M) version.
        read = cli.run_ok("bw", "read", "ADSO", name, "--version", "m")
        assert read["name"] == name

    def test_create_adso_by_copy(self, cli, bw_has_adso, adso_cleanup):
        """Copy-create carries the source definition into the new object."""
        sources = cli.run_ok("bw", "search", "*", "--max", "1", "--type", "ADSO")
        if not sources:
            pytest.skip("No ADSO source object available to copy from")
        source = sources[0]["name"]

        name = _unique_name()
        result = cli.run("bw", "create", "ADSO", name,
                         "--copy-from-name", source,
                         "--copy-from-type", "ADSO",
                         "--package", "$TMP")
        assert result.returncode == 0, result.stderr
        adso_cleanup.append(name)

        read = cli.run_ok("bw", "read", "ADSO", name, "--version", "m")
        assert read["name"] == name

    def test_create_iobj_by_copy(self, cli, bw_available):
        """InfoObject creation goes through PUT, not POST (issue #44 follow-up).

        The IOBJ resource controller implements only get(), so a POST for a
        name that does not exist yet answers 404 "Resource IOBJ ... does not
        exist"; the client retries with PUT.
        """
        sources = cli.run_ok("bw", "search", "0CALMONTH", "--max", "1", "--type", "IOBJ")
        if not sources:
            pytest.skip("No delivered IOBJ available to copy from")
        source = sources[0]["name"]

        name = ("ZTIO" + uuid.uuid4().hex[:4]).upper()
        result = cli.run("bw", "create", "IOBJ", name,
                         "--copy-from-name", source, "--copy-from-type", "IOBJ")
        assert result.returncode == 0, result.stderr
        assert "does not exist" not in result.stderr

        read = cli.run_ok("bw", "read", "IOBJ", name, "--version", "m")
        assert read["name"] == name

    def test_copy_does_not_rename_a_longer_neighbour(self, cli, bw_available):
        """Copying 0CALMONTH must leave a referenced 0CALMONTH2 alone."""
        sources = cli.run_ok("bw", "search", "0CALMONTH", "--max", "1", "--type", "IOBJ")
        if not sources:
            pytest.skip("No delivered IOBJ available to copy from")

        name = ("ZTIO" + uuid.uuid4().hex[:4]).upper()
        created = cli.run("bw", "create", "IOBJ", name,
                          "--copy-from-name", "0CALMONTH", "--copy-from-type", "IOBJ")
        if created.returncode != 0:
            pytest.skip("IOBJ copy-create unavailable on this system")

        # A substring rename produced references to "<name>2", an object that
        # does not exist; activation then reported it as missing.
        activated = cli.run("bw", "activate", "IOBJ", name)
        combined = activated.stdout + activated.stderr
        assert name + "2" not in combined

    def test_create_without_body_reports_a_usable_error(self, cli, bw_has_adso):
        """A type with no built-in template fails before the wire, with a hint."""
        result = cli.run("bw", "create", "TRFN", _unique_name())
        assert result.returncode != 0
        combined = (result.stdout + result.stderr).lower()
        assert "--file" in combined or "copy-from" in combined
        # Not a raw backend 500 / 415
        assert "500" not in combined
        assert "415" not in combined


@pytest.mark.bw
class TestBwReadVersion:

    def test_version_reaches_the_request_url(self, cli, bw_has_adso):
        """--version must appear in the request path, not just in the output."""
        objects = cli.run_ok("bw", "search", "*", "--max", "1", "--type", "ADSO")
        if not objects:
            pytest.skip("No ADSO object available for the version read test")
        name = objects[0]["name"]

        result = cli.run_no_json("-v", "bw", "read", "ADSO", name, "--version", "m")
        requests = [line for line in result.stderr.splitlines()
                    if "/sap/bw/modeling/adso/" in line]
        assert requests, result.stderr
        assert any(line.rstrip().endswith("/m") for line in requests), \
            "the requested version never reached the URL:\n" + "\n".join(requests)


@pytest.mark.bw
class TestBwSave:
    """Saving addresses a version segment (issue #44 sweep).

    `PUT /sap/bw/modeling/{tlogo}/{name}` answers HTTP 400 "Parameter version
    could not be found", so every save had failed before this.
    """

    def test_lock_edit_save_round_trip(self, cli, bw_has_adso, adso_cleanup, tmp_path):
        name = _unique_name()
        created = cli.run("bw", "create", "ADSO", name, "--package", "$TMP")
        if created.returncode != 0:
            pytest.skip("bw create unavailable; nothing to save")
        adso_cleanup.append(name)

        session_file = str(tmp_path / "save_session.json")
        lock = cli.run("bw", "lock", "ADSO", name, session_file=session_file)
        assert lock.returncode == 0, lock.stderr
        handle = json.loads(lock.stdout.strip())["lock_handle"]

        raw = cli.run_no_json("bw", "read", "ADSO", name, "--version", "m", "--raw",
                              session_file=session_file)
        assert raw.returncode == 0
        edited = raw.stdout.replace("<endUserTexts label=\"%s\"/>" % name,
                                    "<endUserTexts label=\"edited by erpl-adt\"/>")
        payload = tmp_path / "edited.xml"
        payload.write_text(edited, encoding="utf-8")

        saved = cli.run("bw", "save", "ADSO", name, "--lock-handle", handle,
                        "--file", str(payload), session_file=session_file)
        cli.run("bw", "unlock", "ADSO", name, session_file=session_file)

        assert saved.returncode == 0, saved.stderr
        assert "Parameter version" not in (saved.stdout + saved.stderr)

        back = cli.run_no_json("bw", "read", "ADSO", name, "--version", "m", "--raw")
        assert "edited by erpl-adt" in back.stdout


@pytest.mark.bw
class TestBwActivate:
    """Activation over the real endpoint (issue #44).

    Every `bw activate` call used to answer HTTP 500 "Request cannot be
    deserialized" — the payload was a `bwActivation:objects` document the
    backend never accepted. It takes an Atom feed with one entry instead.
    """

    def test_activate_reports_check_messages(self, cli, bw_has_adso, adso_cleanup):
        """Activating an incomplete object returns its check errors, not a 500."""
        name = _unique_name()
        created = cli.run("bw", "create", "ADSO", name, "--package", "$TMP")
        if created.returncode != 0:
            pytest.skip("bw create unavailable; activation has nothing to act on")
        adso_cleanup.append(name)

        result = cli.run("bw", "activate", "ADSO", name)
        combined = result.stdout + result.stderr
        # A minimal ADSO has no fields, so activation legitimately fails — but
        # it must fail with the backend's modelling messages, not a protocol
        # error.
        assert "cannot be deserialized" not in combined
        assert '"http_status":500' not in combined
        if result.returncode == 0:
            data = json.loads(result.stdout.strip())
            assert "messages" in data
        else:
            assert "message" in combined.lower() or "consistent" in combined.lower()

    def test_validate_a_consistent_object_succeeds(self, cli, bw_available):
        """A check run against delivered content reports success."""
        objects = cli.run_ok("bw", "search", "0CALMONTH", "--max", "1", "--type", "IOBJ")
        if not objects:
            pytest.skip("No delivered IOBJ available for a check run")
        name = objects[0]["name"]

        # --validate goes to /checkruns: it checks and changes nothing, which
        # is why it is safe to point at delivered content.
        data = cli.run_ok("bw", "activate", "IOBJ", name, "--validate")
        assert data["success"] is True
        assert not [m for m in data.get("messages", []) if m["severity"] == "E"]

    def test_validate_does_not_activate(self, cli, bw_available):
        """--validate must reach the checkruns endpoint, not the activation one."""
        objects = cli.run_ok("bw", "search", "0CALMONTH", "--max", "1", "--type", "IOBJ")
        if not objects:
            pytest.skip("No delivered IOBJ available for a check run")

        result = cli.run_no_json("-v", "bw", "activate", "IOBJ", objects[0]["name"],
                                 "--validate")
        assert "/sap/bw/modeling/checkruns" in result.stderr
        assert "POST /sap/bw/modeling/activation" not in result.stderr
