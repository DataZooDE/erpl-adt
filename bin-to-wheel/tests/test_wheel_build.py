"""Epic 4: Wheel ZIP assembly tests."""

import zipfile
import csv
import io

from bin_to_wheel import build_wheel, compute_file_hash


def test_returns_path(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
    )
    assert path.exists()
    assert path.suffix == ".whl"


def test_filename_format(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="2026.2.14",
        platform_tag="manylinux_2_17_x86_64",
    )
    assert path.name == "erpl_adt-2026.2.14-py3-none-manylinux_2_17_x86_64.whl"


def test_is_valid_zip(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
    )
    assert zipfile.is_zipfile(path)


def test_contains_binary(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
        entry_point="erpl-adt",
    )
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
        assert "erpl_adt/bin/erpl-adt" in names


def test_contains_init_and_main(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
    )
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
        assert "erpl_adt/__init__.py" in names
        assert "erpl_adt/__main__.py" in names


def test_contains_dist_info(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
    )
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
        assert "erpl_adt-1.0.0.dist-info/METADATA" in names
        assert "erpl_adt-1.0.0.dist-info/WHEEL" in names
        assert "erpl_adt-1.0.0.dist-info/RECORD" in names


def test_binary_permissions(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
        entry_point="erpl-adt",
    )
    with zipfile.ZipFile(path) as zf:
        info = zf.getinfo("erpl_adt/bin/erpl-adt")
        unix_perms = (info.external_attr >> 16) & 0o777
        assert unix_perms & 0o755 == 0o755


def test_record_hashes_match(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
    )
    with zipfile.ZipFile(path) as zf:
        record_data = zf.read("erpl_adt-1.0.0.dist-info/RECORD").decode("utf-8")
        reader = csv.reader(io.StringIO(record_data))
        for row in reader:
            if len(row) >= 2 and row[1]:  # skip RECORD itself (empty hash)
                filepath, hash_entry, size = row
                content = zf.read(filepath)
                expected = f"sha256={compute_file_hash(content)}"
                assert hash_entry == expected, f"Hash mismatch for {filepath}"
                assert int(size) == len(content), f"Size mismatch for {filepath}"


def test_entry_points_present(fake_binary, output_dir):
    path = build_wheel(
        binary_path=fake_binary,
        output_dir=output_dir,
        name="erpl-adt",
        version="1.0.0",
        platform_tag="manylinux_2_17_x86_64",
        entry_point="erpl-adt",
    )
    with zipfile.ZipFile(path) as zf:
        names = zf.namelist()
        assert "erpl_adt-1.0.0.dist-info/entry_points.txt" in names
        ep = zf.read("erpl_adt-1.0.0.dist-info/entry_points.txt").decode("utf-8")
        assert "erpl-adt = erpl_adt:main" in ep
