"""Epic 1: Name normalization tests."""

from bin_to_wheel import normalize_package_name


def test_hyphens_become_underscores():
    assert normalize_package_name("erpl-adt") == "erpl_adt"


def test_dots_become_underscores():
    assert normalize_package_name("my.package") == "my_package"


def test_already_normalized():
    assert normalize_package_name("erpl_adt") == "erpl_adt"


def test_uppercase_lowered():
    assert normalize_package_name("My-Package") == "my_package"


def test_mixed_separators():
    assert normalize_package_name("My.Cool-Package") == "my_cool_package"
