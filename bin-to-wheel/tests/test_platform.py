"""Epic 5: Platform tag handling tests."""

import platform as stdlib_platform

from bin_to_wheel import PLATFORM_TAGS, resolve_platform_tag, detect_current_platform


def test_friendly_name_linux():
    assert resolve_platform_tag("linux-x86_64") == "manylinux_2_17_x86_64"


def test_friendly_name_macos_arm64():
    assert resolve_platform_tag("macos-arm64") == "macosx_11_0_arm64"


def test_friendly_name_macos_x86_64():
    assert resolve_platform_tag("macos-x86_64") == "macosx_10_15_x86_64"


def test_friendly_name_windows():
    assert resolve_platform_tag("windows-x64") == "win_amd64"


def test_raw_tag_passthrough():
    assert resolve_platform_tag("manylinux_2_17_x86_64") == "manylinux_2_17_x86_64"


def test_raw_tag_passthrough_windows():
    assert resolve_platform_tag("win_amd64") == "win_amd64"


def test_unknown_tag_raises():
    import pytest
    with pytest.raises(ValueError, match="Unknown platform"):
        resolve_platform_tag("freebsd-amd64")


def test_platform_tags_dict_has_four_entries():
    assert len(PLATFORM_TAGS) == 4
    assert set(PLATFORM_TAGS.keys()) == {"linux-x86_64", "macos-arm64", "macos-x86_64", "windows-x64"}


def test_detect_current_platform_returns_valid_tag():
    tag = detect_current_platform()
    # Should be a valid tag — either a friendly name value or a raw tag
    assert isinstance(tag, str)
    assert len(tag) > 0
    # Should be resolvable
    resolved = resolve_platform_tag(tag)
    assert isinstance(resolved, str)
