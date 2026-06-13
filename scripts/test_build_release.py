"""Unit tests for the pure helpers in build_release.py.

These cover the parts that do not need the ARM toolchain or a real firmware build: the
banner literal, checksum/manifest/flashing rendering, and argument parsing. The build
orchestration itself (run_make/build_engine) is exercised by actually running
`make dist`, not here.
"""

import hashlib

import pytest

import build_release as m


def test_banner_bytes_is_nul_terminated_literal():
    assert m.banner_bytes("0.3.0", "reverb") == b"spotykach 0.3.0 engine=reverb\x00"
    # The trailing NUL keeps it from matching as a prefix of a longer string.
    assert m.banner_bytes("0.3.0", "rev") not in m.banner_bytes("0.3.0", "reverb")


def test_sha256_matches_hashlib(tmp_path):
    f = tmp_path / "blob.bin"
    data = b"\x00\x01\x02" * 1000
    f.write_bytes(data)
    assert m.sha256(f) == hashlib.sha256(data).hexdigest()


def test_write_checksums_format_and_scope(tmp_path):
    # Only .bin/.hex are checksummed; other files (manifest etc.) are excluded.
    (tmp_path / "a.bin").write_bytes(b"aaa")
    (tmp_path / "b.hex").write_bytes(b"bbb")
    (tmp_path / "MANIFEST.txt").write_text("ignore me")

    m.write_checksums(tmp_path)
    lines = (tmp_path / "SHA256SUMS").read_text().splitlines()

    assert len(lines) == 2  # MANIFEST.txt excluded
    names = []
    for line in lines:
        digest, name = line.split("  ", 1)  # `shasum -a 256 -c` format: two spaces
        assert len(digest) == 64
        names.append(name)
    assert names == sorted(names)  # deterministic ordering
    assert "MANIFEST.txt" not in names


def test_write_checksums_digest_is_correct(tmp_path):
    (tmp_path / "a.bin").write_bytes(b"hello")
    m.write_checksums(tmp_path)
    digest, name = (tmp_path / "SHA256SUMS").read_text().split()
    assert name == "a.bin"
    assert digest == hashlib.sha256(b"hello").hexdigest()


def test_write_manifest_contents(tmp_path):
    path = tmp_path / "MANIFEST.txt"
    m.write_manifest(path, "0.3.0", " (dirty tree - not a clean release build)", "abc1234",
                     {"reverb": 175384, "delay": 153680})
    text = path.read_text()
    assert "version:    0.3.0 (dirty tree" in text
    assert "git commit: abc1234" in text
    assert "reverb" in text and "175384" in text
    assert "sk-delay-0.3.0.bin" in text


def test_write_flashing_has_address_and_version(tmp_path):
    path = tmp_path / "FLASHING.md"
    m.write_flashing(path, "0.3.0")
    text = path.read_text()
    assert m.APP_ADDRESS in text          # QSPI app address
    assert f",0483:{m.DFU_PID}" in text
    assert m.WEB_PROGRAMMER_URL in text   # web flasher is the recommended path
    assert "sk-<engine>-0.3.0.bin" in text


def test_write_flashing_has_no_bootloader_install_commands(tmp_path):
    path = tmp_path / "FLASHING.md"
    m.write_flashing(path, "0.3.0")
    text = path.read_text()
    # The bootloader-install procedure was deliberately removed (unverified / bricking-adjacent).
    assert "0x08000000" not in text
    assert "bootloader-spotykach-v2.bin" not in text


def test_parse_args_defaults():
    args = m.parse_args([])
    assert args.version is None
    assert args.engines == []
    assert args.jobs == 8
    assert args.hex is False  # .hex omitted unless explicitly requested


def test_parse_args_positional():
    args = m.parse_args(["0.3.0", "reverb", "delay"])
    assert args.version == "0.3.0"
    assert args.engines == ["reverb", "delay"]


def test_parse_args_hex_flag():
    assert m.parse_args(["0.3.0", "--hex"]).hex is True
    assert m.parse_args(["0.3.0", "reverb", "--hex"]).engines == ["reverb"]


def test_parse_args_jobs_flag():
    assert m.parse_args(["-j", "4"]).jobs == 4
    assert m.parse_args(["--jobs", "2"]).jobs == 2
