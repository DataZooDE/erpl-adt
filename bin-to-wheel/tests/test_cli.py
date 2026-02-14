"""Epic 6: CLI tests."""

import subprocess
import sys
import zipfile


def run_cli(*args):
    return subprocess.run(
        [sys.executable, "-m", "bin_to_wheel", *args],
        capture_output=True,
        text=True,
    )


def test_help_flag():
    r = run_cli("--help")
    assert r.returncode == 0
    assert "bin-to-wheel" in r.stdout.lower() or "--name" in r.stdout


def test_missing_required_args():
    r = run_cli()
    assert r.returncode != 0


def test_platform_and_auto_platform_exclusive():
    r = run_cli(
        "--name", "foo",
        "--version", "1.0",
        "--binary", "/dev/null",
        "--platform", "linux-x86_64",
        "--auto-platform",
    )
    assert r.returncode != 0
    assert "not allowed" in r.stderr.lower() or "exclusive" in r.stderr.lower()


def test_builds_wheel(fake_binary, output_dir):
    r = run_cli(
        "--name", "test-pkg",
        "--version", "0.1.0",
        "--binary", str(fake_binary),
        "--platform", "linux-x86_64",
        "--output-dir", str(output_dir),
    )
    assert r.returncode == 0, f"stderr: {r.stderr}"
    wheels = list(output_dir.glob("*.whl"))
    assert len(wheels) == 1


def test_auto_platform_builds_wheel(fake_binary, output_dir):
    r = run_cli(
        "--name", "test-pkg",
        "--version", "0.1.0",
        "--binary", str(fake_binary),
        "--auto-platform",
        "--output-dir", str(output_dir),
    )
    assert r.returncode == 0, f"stderr: {r.stderr}"
    wheels = list(output_dir.glob("*.whl"))
    assert len(wheels) == 1


def test_entry_point_flag(fake_binary, output_dir):
    r = run_cli(
        "--name", "my-tool",
        "--version", "1.0.0",
        "--binary", str(fake_binary),
        "--platform", "linux-x86_64",
        "--output-dir", str(output_dir),
        "--entry-point", "my-tool",
    )
    assert r.returncode == 0, f"stderr: {r.stderr}"
    whl = list(output_dir.glob("*.whl"))[0]
    with zipfile.ZipFile(whl) as zf:
        ep = zf.read("my_tool-1.0.0.dist-info/entry_points.txt").decode()
        assert "my-tool = my_tool:main" in ep


def test_output_path_printed(fake_binary, output_dir):
    r = run_cli(
        "--name", "test-pkg",
        "--version", "0.1.0",
        "--binary", str(fake_binary),
        "--platform", "linux-x86_64",
        "--output-dir", str(output_dir),
    )
    assert r.returncode == 0
    assert ".whl" in r.stdout


def test_binary_not_found(output_dir):
    r = run_cli(
        "--name", "test-pkg",
        "--version", "0.1.0",
        "--binary", "/nonexistent/binary",
        "--platform", "linux-x86_64",
        "--output-dir", str(output_dir),
    )
    assert r.returncode != 0
