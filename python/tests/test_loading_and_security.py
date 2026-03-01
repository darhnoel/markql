import pathlib

import pytest

import xsql


def test_load_local_file_with_base_dir(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    doc = xsql.load(str(html_path), base_dir=str(tmp_path))
    assert doc.source == str(html_path)
    assert "<a" in doc.html


def test_base_dir_traversal_blocked(tmp_path: pathlib.Path) -> None:
    outside = tmp_path.parent / "outside.html"
    outside.write_text("<html></html>", encoding="utf-8")
    with pytest.raises(ValueError):
        xsql.load(str(outside), base_dir=str(tmp_path))


def test_network_disabled_by_default() -> None:
    with pytest.raises(ValueError):
        xsql.load("https://example.com")


def test_ssrf_blocks_localhost() -> None:
    with pytest.raises(ValueError):
        xsql.load("http://127.0.0.1/", allow_network=True)


def test_max_bytes_enforced(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "big.html"
    html_path.write_bytes(b"<html>" + b"a" * 64 + b"</html>")
    with pytest.raises(ValueError):
        xsql.load(str(html_path), base_dir=str(tmp_path), max_bytes=16)


def test_large_html_fails_fast() -> None:
    html = "<div></div>" * 100_001
    with pytest.raises(ValueError):
        xsql.load(html)
