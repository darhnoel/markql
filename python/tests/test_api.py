import pathlib

import pytest

import xsql


def _supports_project() -> bool:
    doc = xsql.load("<table><tbody><tr><td>2025</td></tr></tbody></table>")
    try:
        xsql.execute(
            "SELECT PROJECT(tr) AS (period: TEXT(td WHERE sibling_pos = 1)) "
            "FROM document WHERE EXISTS(child WHERE tag = 'td')",
            doc=doc,
        )
        return True
    except RuntimeError:
        return False


def _supports_describe_language() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        xsql.execute("DESCRIBE language", doc=doc)
        return True
    except RuntimeError:
        return False


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


def test_execute_project_projection() -> None:
    # PROJECT should expose computed alias columns through Python bindings.
    if not _supports_project():
        pytest.skip("native xsql._core module does not include PROJECT support yet")
    doc = xsql.load(
        "<table><tbody>"
        "<tr><td>2025</td><td><a href='direct.pdf'>PDF</a></td></tr>"
        "<tr><td>2024</td><td>Pending</td></tr>"
        "</tbody></table>"
    )
    result = xsql.execute(
        "SELECT tr.node_id, "
        "PROJECT(tr) AS ("
        "period: TEXT(td WHERE sibling_pos = 1),"
        "pdf_direct: COALESCE("
        "ATTR(a, href WHERE parent.sibling_pos = 2 AND href CONTAINS '.pdf'),"
        "TEXT(td WHERE sibling_pos = 2)"
        ")"
        ") "
        "FROM document "
        "WHERE EXISTS(child WHERE tag = 'td')",
        doc=doc,
    )
    assert result.columns == ["node_id", "period", "pdf_direct"]
    assert len(result.rows) == 2
    assert result.rows[0]["period"] == "2025"
    assert result.rows[0]["pdf_direct"] == "direct.pdf"
    assert result.rows[1]["period"] == "2024"
    assert result.rows[1]["pdf_direct"] == "Pending"


def test_execute_flatten_extract_alias_compatibility() -> None:
    # FLATTEN_EXTRACT remains an alias for compatibility.
    if not _supports_project():
        pytest.skip("native xsql._core module does not include PROJECT support yet")
    doc = xsql.load("<table><tbody><tr><td>2025</td></tr></tbody></table>")
    result = xsql.execute(
        "SELECT FLATTEN_EXTRACT(tr) AS (period: TEXT(td WHERE sibling_pos = 1)) "
        "FROM document "
        "WHERE EXISTS(child WHERE tag = 'td')",
        doc=doc,
    )
    assert len(result.rows) == 1
    assert result.rows[0]["period"] == "2025"


def test_describe_language_lists_project() -> None:
    # DESCRIBE language should include PROJECT as canonical function entry.
    if not _supports_describe_language():
        pytest.skip("native xsql._core module does not include DESCRIBE language yet")
    doc = xsql.load("<html><body></body></html>")
    result = xsql.execute("DESCRIBE language", doc=doc)
    assert any(
        row.get("category") == "function" and row.get("name") == "project"
        for row in result.rows
    )


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
