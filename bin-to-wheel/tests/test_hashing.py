"""Epic 2: RECORD hash computation tests."""

import hashlib
import base64

from bin_to_wheel import compute_file_hash


def test_known_content():
    content = b"hello world\n"
    digest = hashlib.sha256(content).digest()
    expected = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    assert compute_file_hash(content) == expected


def test_empty_content():
    content = b""
    digest = hashlib.sha256(content).digest()
    expected = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    assert compute_file_hash(content) == expected


def test_binary_content():
    content = bytes(range(256))
    digest = hashlib.sha256(content).digest()
    expected = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    assert compute_file_hash(content) == expected
