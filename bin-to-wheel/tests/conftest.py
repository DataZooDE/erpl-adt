import stat
import pytest


@pytest.fixture
def fake_binary(tmp_path):
    """Create a fake binary file for testing."""
    binary = tmp_path / "fake-binary"
    binary.write_bytes(b"#!/bin/sh\necho hello\n")
    binary.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    return binary


@pytest.fixture
def output_dir(tmp_path):
    """Provide a clean output directory for wheel files."""
    d = tmp_path / "dist"
    d.mkdir()
    return d
