"""Epic 7: Integration tests with real erpl-adt binary."""

import subprocess
import sys
from pathlib import Path

import pytest

BINARY_PATH = Path(__file__).parent.parent.parent / "build" / "erpl-adt"


@pytest.mark.integration
class TestIntegration:
    @pytest.fixture(autouse=True)
    def skip_if_no_binary(self):
        if not BINARY_PATH.exists():
            pytest.skip(f"Binary not found: {BINARY_PATH}")

    def test_build_and_run_version(self, tmp_path):
        dist = tmp_path / "dist"
        dist.mkdir()

        # Build the wheel
        r = subprocess.run(
            [
                sys.executable, "-m", "bin_to_wheel",
                "--name", "erpl-adt",
                "--version", "0.0.1",
                "--binary", str(BINARY_PATH),
                "--auto-platform",
                "--output-dir", str(dist),
                "--entry-point", "erpl-adt",
                "--description", "Test wheel",
            ],
            capture_output=True,
            text=True,
        )
        assert r.returncode == 0, f"Build failed: {r.stderr}"

        wheels = list(dist.glob("*.whl"))
        assert len(wheels) == 1
        whl = wheels[0]

        # Install into a temp venv and run
        venv = tmp_path / "venv"
        subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
        pip = venv / "bin" / "pip"
        subprocess.run([str(pip), "install", str(whl)], check=True, capture_output=True)

        erpl_adt = venv / "bin" / "erpl-adt"
        r2 = subprocess.run([str(erpl_adt), "--version"], capture_output=True, text=True)
        assert r2.returncode == 0, f"--version failed: {r2.stderr}"
        assert "erpl-adt" in r2.stdout.lower() or len(r2.stdout.strip()) > 0

    def test_wheel_filename_matches_platform(self, tmp_path):
        dist = tmp_path / "dist"
        dist.mkdir()

        r = subprocess.run(
            [
                sys.executable, "-m", "bin_to_wheel",
                "--name", "erpl-adt",
                "--version", "0.0.1",
                "--binary", str(BINARY_PATH),
                "--auto-platform",
                "--output-dir", str(dist),
            ],
            capture_output=True,
            text=True,
        )
        assert r.returncode == 0

        wheels = list(dist.glob("*.whl"))
        assert len(wheels) == 1
        name = wheels[0].name
        # Should contain a platform-specific tag, not the generic "any" tag
        assert "-py3-none-any.whl" not in name
        assert "py3-none-" in name
