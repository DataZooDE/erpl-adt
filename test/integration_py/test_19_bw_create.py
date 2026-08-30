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
