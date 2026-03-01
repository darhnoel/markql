import pytest

import xsql


def test_parse_source_in_python_binding(supports_parse_source: bool) -> None:
    if not supports_parse_source:
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


def test_fragments_deprecation_warning_in_python_binding(supports_fragments_warnings: bool) -> None:
    if not supports_fragments_warnings:
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


def test_execute_project_projection(supports_project: bool) -> None:
    if not supports_project:
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


def test_execute_flatten_extract_alias_compatibility(supports_project: bool) -> None:
    if not supports_project:
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


def test_describe_language_lists_project(supports_describe_language: bool) -> None:
    if not supports_describe_language:
        pytest.skip("native xsql._core module does not include DESCRIBE language yet")
    doc = xsql.load("<html><body></body></html>")
    result = xsql.execute("DESCRIBE language", doc=doc)
    assert any(
        row.get("category") == "function" and row.get("name") == "project"
        for row in result.rows
    )
