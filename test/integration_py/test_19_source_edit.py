"""source edit integration tests — read→edit→write round-trip via CLI."""

import os
import stat
import subprocess
import sys
import tempfile

import pytest


def _run_with_editor(cli, editor_cmd, *extra_args, timeout=60):
    """Run `source edit` with a custom EDITOR env var.

    cli: CliRunner instance (provides binary path + connection flags).
    editor_cmd: shell command string used as EDITOR (e.g. "true", "/path/to/script.sh").
    extra_args: additional args passed to `source edit`.

    Returns CompletedProcess.
    """
    env = os.environ.copy()
    env["VISUAL"] = ""       # Prefer EDITOR over VISUAL in our tests.
    env["EDITOR"] = editor_cmd

    cmd = [cli.binary]
    # Add connection flags (skip --json=true — this command writes human-readable output).
    for a in cli.base_args:
        if a != "--json=true":
            cmd.append(a)
    cmd += ["source", "edit"] + [str(a) for a in extra_args]

    masked_cmd = [a if "--password" not in a else "***" for a in cmd]
    print(f"\n$ EDITOR={editor_cmd!r} {' '.join(masked_cmd)}", file=sys.stderr)

    result = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
    )
    if result.returncode != 0:
        print(f"  -> exit {result.returncode}", file=sys.stderr)
        if result.stderr:
            print(f"  stderr: {result.stderr[:500]}", file=sys.stderr)
    return result


@pytest.mark.source
class TestSourceEdit:

    def test_edit_no_change(self, test_class, cli):
        """source edit with no-op EDITOR exits 0 and prints 'No changes'."""
        source_uri = test_class["uri"] + "/source/main"

        # EDITOR=true: does nothing, temp file stays unchanged.
        result = _run_with_editor(cli, "true", source_uri)

        assert result.returncode == 0, (
            f"Expected exit 0, got {result.returncode}.\n"
            f"stderr: {result.stderr}"
        )
        combined = result.stdout + result.stderr
        assert "No changes" in combined, (
            f"Expected 'No changes' in output, got:\n{combined[:500]}"
        )

    def test_edit_write_back(self, test_class, cli, tmp_path):
        """source edit with modifying EDITOR writes back changed source."""
        source_uri = test_class["uri"] + "/source/main"
        name = test_class["name"]

        # Read original source first so we can restore it.
        read_result = cli.run("source", "read", source_uri)
        assert read_result.returncode == 0, f"Failed to read original: {read_result.stderr}"
        import json
        original_source = json.loads(read_result.stdout.strip()).get("source", "")

        # Build a sentinel comment that's stable and unique.
        sentinel = f"* erpl-adt-test-{name.lower()}"

        # Create an editor script that appends the sentinel comment.
        editor_script = tmp_path / "append_comment.sh"
        editor_script.write_text(
            f"#!/bin/sh\n"
            f"printf '\\n{sentinel}\\n' >> \"$1\"\n"
        )
        editor_script.chmod(editor_script.stat().st_mode | stat.S_IEXEC)

        try:
            result = _run_with_editor(cli, str(editor_script), source_uri)
            assert result.returncode == 0, (
                f"source edit failed (exit {result.returncode}).\n"
                f"stderr: {result.stderr}"
            )
            combined = result.stdout + result.stderr
            assert "Source written" in combined, (
                f"Expected 'Source written' in output:\n{combined[:500]}"
            )

            # Verify the sentinel is present in the inactive version.
            verify = cli.run("source", "read", source_uri, "--version", "inactive")
            if verify.returncode == 0:
                verify_data = json.loads(verify.stdout.strip())
                assert sentinel in verify_data.get("source", ""), (
                    f"Sentinel not found in inactive source after write.\n"
                    f"Source: {verify_data.get('source', '')[:300]}"
                )
        finally:
            # Restore original source so the SAP system stays clean.
            if original_source:
                with tempfile.NamedTemporaryFile(
                    mode="w", suffix=".abap", delete=False
                ) as f:
                    f.write(original_source)
                    restore_path = f.name
                try:
                    cli.run("source", "write", source_uri, "--file", restore_path)
                finally:
                    os.unlink(restore_path)

    def test_edit_no_write_flag(self, test_class, cli, tmp_path):
        """source edit --no-write opens editor but does not persist changes."""
        source_uri = test_class["uri"] + "/source/main"

        # Editor that always modifies the file.
        editor_script = tmp_path / "modify.sh"
        editor_script.write_text(
            "#!/bin/sh\n"
            "printf '\\n* THIS SHOULD NOT APPEAR\\n' >> \"$1\"\n"
        )
        editor_script.chmod(editor_script.stat().st_mode | stat.S_IEXEC)

        result = _run_with_editor(cli, str(editor_script), source_uri, "--no-write")
        assert result.returncode == 0, (
            f"source edit --no-write failed (exit {result.returncode}).\n"
            f"stderr: {result.stderr}"
        )
        combined = result.stdout + result.stderr
        assert "No changes written" in combined, (
            f"Expected 'No changes written' in output:\n{combined[:500]}"
        )

    def test_edit_missing_arg_returns_99(self, cli):
        """source edit with no positional arg exits 99."""
        result = _run_with_editor(cli, "true")
        assert result.returncode == 99, (
            f"Expected exit 99 for missing arg, got {result.returncode}."
        )

    def test_edit_invalid_section_returns_99(self, test_class, cli):
        """source edit with invalid --section exits 99."""
        source_uri = test_class["uri"] + "/source/main"
        result = _run_with_editor(cli, "true", source_uri, "--section", "bogus")
        assert result.returncode == 99

    def test_edit_nonexistent_object_fails(self, cli):
        """source edit of non-existent URI exits non-zero."""
        result = _run_with_editor(
            cli, "true",
            "/sap/bc/adt/oo/classes/znonexistent_99999/source/main"
        )
        assert result.returncode != 0
