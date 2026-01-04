import pathlib

import pytest

import xsql


def test_load_local_file_with_base_dir(tmp_path: pathlib.Path) -> None:
    # Load from an allowed base_dir to ensure safe local access.
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    doc = xsql.load(str(html_path), base_dir=str(tmp_path))
    assert doc.source == str(html_path)
    assert "<a" in doc.html


def test_base_dir_traversal_blocked(tmp_path: pathlib.Path) -> None:
    # Block traversal outside base_dir even when the path is explicit.
    outside = tmp_path.parent / "outside.html"
    outside.write_text("<html></html>", encoding="utf-8")
    with pytest.raises(ValueError):
        xsql.load(str(outside), base_dir=str(tmp_path))


def test_network_disabled_by_default() -> None:
    # Network is opt-in to prevent SSRF by default.
    with pytest.raises(ValueError):
        xsql.load("https://example.com")


def test_ssrf_blocks_localhost() -> None:
    # Localhost targets should be blocked unless explicitly allowed.
    with pytest.raises(ValueError):
        xsql.load("http://127.0.0.1/", allow_network=True)


def test_max_bytes_enforced(tmp_path: pathlib.Path) -> None:
    # Enforce max_bytes to avoid oversized inputs.
    html_path = tmp_path / "big.html"
    html_path.write_bytes(b"<html>" + b"a" * 64 + b"</html>")
    with pytest.raises(ValueError):
        xsql.load(str(html_path), base_dir=str(tmp_path), max_bytes=16)


def test_execute_uses_doc_fallback(tmp_path: pathlib.Path) -> None:
    # execute() should fall back to the module-level doc when present.
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    xsql.load(str(html_path), base_dir=str(tmp_path))
    result = xsql.execute("SELECT a FROM document")
    assert result.columns
    assert len(result.rows) == 1


def test_execute_with_explicit_doc() -> None:
    # execute() should use an explicit doc argument instead of global state.
    doc = xsql.load("<html><body><div></div></body></html>")
    result = xsql.execute("SELECT div FROM document", doc=doc)
    assert len(result.rows) == 1


def test_execute_examples() -> None:
    # Exercise a few representative queries to validate bindings end-to-end.
    doc = xsql.load("<html><body><a href='x'></a><a></a></body></html>")
    result = xsql.execute("SELECT a.attributes FROM document LIMIT 1", doc=doc)
    assert result.rows[0]["attributes"]["href"] == "x"
    count = xsql.execute("SELECT COUNT(a) FROM document", doc=doc)
    assert count.rows[0]["count"] == 2


def test_summarize_output_shape() -> None:
    # summarize() should return a predictable dictionary structure.
    doc = xsql.load("<html><body><div></div></body></html>")
    summary = xsql.summarize(doc)
    assert "total_nodes" in summary
    assert "tag_counts" in summary
    assert "attribute_keys" in summary


def test_large_html_fails_fast() -> None:
    # Large inputs should fail early to avoid runaway parsing.
    html = "<div></div>" * 100_001
    with pytest.raises(ValueError):
        xsql.load(html)
