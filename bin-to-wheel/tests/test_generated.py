"""Epic 3: Generated file content tests."""

from bin_to_wheel import (
    generate_init_py,
    generate_main_py,
    generate_metadata,
    generate_wheel_metadata,
    generate_entry_points,
    generate_record,
    compute_file_hash,
)


# --- __init__.py ---

def test_init_py_contains_version():
    src = generate_init_py("1.2.3", "my-tool")
    assert '__version__ = "1.2.3"' in src


def test_init_py_contains_binary_name():
    src = generate_init_py("1.0.0", "erpl-adt")
    assert "erpl-adt" in src


def test_init_py_has_main_function():
    src = generate_init_py("1.0.0", "erpl-adt")
    assert "def main():" in src


def test_init_py_has_get_binary_path():
    src = generate_init_py("1.0.0", "erpl-adt")
    assert "def get_binary_path()" in src


def test_init_py_handles_chmod():
    src = generate_init_py("1.0.0", "erpl-adt")
    assert "chmod" in src or "stat" in src


# --- __main__.py ---

def test_main_py_imports_main():
    src = generate_main_py()
    assert "from" in src and "main" in src


def test_main_py_calls_main():
    src = generate_main_py()
    assert "main()" in src


# --- METADATA ---

def test_metadata_has_name():
    m = generate_metadata(name="erpl-adt", version="1.0.0")
    assert "Name: erpl-adt" in m


def test_metadata_has_version():
    m = generate_metadata(name="erpl-adt", version="2026.2.14")
    assert "Version: 2026.2.14" in m


def test_metadata_has_metadata_version():
    m = generate_metadata(name="erpl-adt", version="1.0.0")
    assert "Metadata-Version: 2.1" in m


def test_metadata_optional_fields():
    m = generate_metadata(
        name="erpl-adt",
        version="1.0.0",
        description="A CLI tool",
        author="DataZoo GmbH",
        license_name="Apache-2.0",
        url="https://github.com/DataZooDE/erpl-adt",
    )
    assert "Summary: A CLI tool" in m
    assert "Author: DataZoo GmbH" in m
    assert "License: Apache-2.0" in m
    assert "Home-page: https://github.com/DataZooDE/erpl-adt" in m


def test_metadata_without_optional_fields():
    m = generate_metadata(name="foo", version="0.1.0")
    assert "Summary:" not in m
    assert "Author:" not in m


def test_metadata_with_readme():
    m = generate_metadata(name="foo", version="0.1.0", long_description="# Hello\nWorld")
    assert "# Hello\nWorld" in m
    assert "Description-Content-Type: text/markdown" in m


# --- WHEEL ---

def test_wheel_metadata_has_generator():
    w = generate_wheel_metadata("manylinux_2_17_x86_64")
    assert "Generator: bin-to-wheel" in w


def test_wheel_metadata_root_is_purelib_false():
    w = generate_wheel_metadata("manylinux_2_17_x86_64")
    assert "Root-Is-Purelib: false" in w


def test_wheel_metadata_has_tag():
    w = generate_wheel_metadata("manylinux_2_17_x86_64")
    assert "Tag: py3-none-manylinux_2_17_x86_64" in w


def test_wheel_metadata_windows_tag():
    w = generate_wheel_metadata("win_amd64")
    assert "Tag: py3-none-win_amd64" in w


# --- entry_points.txt ---

def test_entry_points_format():
    e = generate_entry_points("erpl-adt", "erpl_adt")
    assert "[console_scripts]" in e
    assert "erpl-adt = erpl_adt:main" in e


def test_entry_points_custom_name():
    e = generate_entry_points("my-cli", "my_cli")
    assert "my-cli = my_cli:main" in e


# --- RECORD ---

def test_record_contains_all_files():
    files = {
        "pkg/__init__.py": b"# init",
        "pkg/bin/tool": b"\x7fELF...",
    }
    r = generate_record(files)
    assert "pkg/__init__.py" in r
    assert "pkg/bin/tool" in r


def test_record_has_correct_hashes():
    content = b"# init"
    files = {"pkg/__init__.py": content}
    r = generate_record(files)
    expected_hash = compute_file_hash(content)
    assert f"sha256={expected_hash}" in r


def test_record_self_entry_has_no_hash():
    files = {"pkg/__init__.py": b"# init"}
    r = generate_record(files, record_path="pkg-1.0.dist-info/RECORD")
    assert "pkg-1.0.dist-info/RECORD,," in r
