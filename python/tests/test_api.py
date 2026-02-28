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


def _supports_parse_source() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        xsql.execute(
            "SELECT li FROM PARSE('<ul><li>1</li></ul>') AS frag",
            doc=doc,
        )
        return True
    except RuntimeError:
        return False


def _supports_fragments_warnings() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        result = xsql.execute(
            "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li></ul>')) AS frag",
            doc=doc,
        )
    except RuntimeError:
        return False
    return bool(result.warnings)


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


def test_parse_source_in_python_binding() -> None:
    # PARSE should work through Python bindings and return deterministic row values.
    if not _supports_parse_source():
        pytest.skip("native xsql._core module does not include PARSE source support yet")
    doc = xsql.load("<html><body></body></html>")
    result = xsql.execute(
        "SELECT li FROM PARSE('<ul><li>1</li><li>2</li></ul>') AS frag ORDER BY node_id",
        doc=doc,
    )
    assert len(result.rows) == 2
    assert result.rows[0]["tag"] == "li"
    assert result.rows[1]["tag"] == "li"
    assert result.warnings == []


def test_fragments_deprecation_warning_in_python_binding() -> None:
    # FRAGMENTS remains supported but should surface deprecation warning in Python results.
    if not _supports_fragments_warnings():
        pytest.skip("native xsql._core module does not include FRAGMENTS warnings yet")
    doc = xsql.load("<html><body></body></html>")
    result = xsql.execute(
        "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag ORDER BY node_id",
        doc=doc,
    )
    assert len(result.rows) == 2
    assert result.rows[0]["tag"] == "li"
    assert result.rows[1]["tag"] == "li"
    assert result.warnings
    assert "deprecated" in result.warnings[0].lower()


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


def test_python_alias_field_implicit_doc_and_explicit_alias_match() -> None:
    # implicit `document` alias (`doc.*`) and explicit alias should produce identical rows.
    doc = xsql.load(
        "<div class='keep'>One</div>"
        "<div class='keep'>Two</div>"
        "<span class='keep'>Skip</span>"
    )
    implicit = xsql.execute(
        "SELECT doc.node_id, doc.tag "
        "FROM document "
        "WHERE doc.tag = 'div' "
        "ORDER BY node_id",
        doc=doc,
    )
    explicit = xsql.execute(
        "SELECT n.node_id, n.tag "
        "FROM document AS n "
        "WHERE n.tag = 'div' "
        "ORDER BY node_id",
        doc=doc,
    )
    assert implicit.rows == explicit.rows
    assert len(implicit.rows) == 2
    assert implicit.rows[0]["tag"] == "div"
    assert implicit.rows[1]["tag"] == "div"


def test_python_text_accepts_alias_binding() -> None:
    # TEXT(alias) should work with both implicit `doc` and explicit alias bindings.
    doc = xsql.load(
        "<div class='keep'>One</div>"
        "<div class='keep'>Two</div>"
        "<span class='keep'>Skip</span>"
    )
    implicit = xsql.execute(
        "SELECT doc.node_id, TEXT(doc) "
        "FROM document "
        "WHERE doc.attributes.class = 'keep' AND doc.tag = 'div' "
        "ORDER BY node_id",
        doc=doc,
    )
    explicit = xsql.execute(
        "SELECT n.node_id, TEXT(n) "
        "FROM document AS n "
        "WHERE n.attributes.class = 'keep' AND n.tag = 'div' "
        "ORDER BY node_id",
        doc=doc,
    )
    assert implicit.rows == explicit.rows
    assert [row["text"] for row in implicit.rows] == ["One", "Two"]


def test_python_doc_identifier_rejected_when_document_is_realiased() -> None:
    # once document is re-aliased, `doc.*` should raise the same binding error.
    doc = xsql.load("<div>One</div>")
    with pytest.raises(RuntimeError, match=r"Identifier 'doc' is not bound; did you mean 'n'\?"):
        xsql.execute("SELECT doc.node_id FROM document AS n WHERE n.tag = 'div'", doc=doc)


def test_python_duplicate_source_alias_error() -> None:
    # duplicate source aliases should fail parse with a clear message.
    doc = xsql.load("<div>One</div>")
    with pytest.raises(RuntimeError, match=r"Query parse error: Duplicate source alias 'n' in FROM"):
        xsql.execute("SELECT n.node_id FROM document AS n AS m WHERE n.tag = 'div'", doc=doc)


def test_python_attr_shorthand_for_attributes_paths() -> None:
    # `attr` should be a shorthand alias of `attributes` in self/alias/axis paths.
    doc = xsql.load(
        "<div id='root'><span id='child'></span></div>"
        "<div id='skip'><span></span></div>"
    )
    implicit = xsql.execute(
        "SELECT span FROM document WHERE parent.attr.id = 'root'",
        doc=doc,
    )
    explicit = xsql.execute(
        "SELECT n FROM document AS n WHERE n.attr.id = 'root'",
        doc=doc,
    )
    assert len(implicit.rows) == 1
    assert implicit.rows[0]["attributes"]["id"] == "child"
    assert explicit.rows == xsql.execute(
        "SELECT n FROM document AS n WHERE n.attributes.id = 'root'",
        doc=doc,
    ).rows


def test_python_with_left_join_lateral_smoke() -> None:
    # Smoke test: WITH + LEFT JOIN + CROSS JOIN LATERAL should return deterministic values.
    doc = xsql.load(
        "<table>"
        "<tr><td>A</td><td>ID-123</td><td>...</td><td>Apple</td></tr>"
        "<tr><td>B</td><td>ID-999</td><td>...</td></tr>"
        "</table>"
    )
    query = (
        "WITH rows AS ("
        "  SELECT n.node_id AS row_id "
        "  FROM document AS n "
        "  WHERE n.tag = 'tr' AND EXISTS(child WHERE tag = 'td')"
        "), "
        "cells AS ("
        "  SELECT r.row_id, c.sibling_pos AS pos, TEXT(c) AS val "
        "  FROM rows AS r "
        "  CROSS JOIN LATERAL ("
        "    SELECT c "
        "    FROM document AS c "
        "    WHERE c.parent_id = r.row_id AND c.tag = 'td'"
        "  ) AS c"
        ") "
        "SELECT r.row_id, c2.val AS item_id, c4.val AS item_name "
        "FROM rows AS r "
        "LEFT JOIN cells AS c2 ON c2.row_id = r.row_id AND c2.pos = 2 "
        "LEFT JOIN cells AS c4 ON c4.row_id = r.row_id AND c4.pos = 4 "
        "ORDER BY r.row_id"
    )
    result = xsql.execute(query, doc=doc)
    assert result.columns == ["row_id", "item_id", "item_name"]
    assert len(result.rows) == 2
    assert result.rows[0]["item_id"] == "ID-123"
    assert result.rows[0]["item_name"] == "Apple"
    assert result.rows[1]["item_id"] == "ID-999"
    assert result.rows[1]["item_name"] is None


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


def test_python_lint_returns_structured_diagnostics() -> None:
    diagnostics = xsql.lint("SELECT FROM doc")
    assert diagnostics
    first = diagnostics[0]
    assert first["severity"] == "ERROR"
    assert first["code"].startswith("MQL-SYN-")
    assert "help" in first and first["help"]
    assert "doc_ref" in first and first["doc_ref"]
    assert "snippet" in first and "^" in first["snippet"]


def test_python_exposes_core_version_provenance() -> None:
    info = xsql.core_version_info()
    assert "version" in info and isinstance(info["version"], str) and info["version"]
    assert "git_commit" in info and isinstance(info["git_commit"], str)
    assert "git_dirty" in info
    assert "provenance" in info and isinstance(info["provenance"], str)
    assert xsql.__version__ == info["version"]
