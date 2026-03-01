import pytest

import xsql


def test_python_alias_field_implicit_doc_and_explicit_alias_match() -> None:
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
    doc = xsql.load("<div>One</div>")
    with pytest.raises(RuntimeError, match=r"Identifier 'doc' is not bound; did you mean 'n'\?"):
        xsql.execute("SELECT doc.node_id FROM document AS n WHERE n.tag = 'div'", doc=doc)


def test_python_duplicate_source_alias_error() -> None:
    doc = xsql.load("<div>One</div>")
    with pytest.raises(RuntimeError, match=r"Query parse error: Duplicate source alias 'n' in FROM"):
        xsql.execute("SELECT n.node_id FROM document AS n AS m WHERE n.tag = 'div'", doc=doc)


def test_python_attr_shorthand_for_attributes_paths() -> None:
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
