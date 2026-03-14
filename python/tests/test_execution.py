import pathlib

import markql


def test_execute_uses_doc_fallback(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    markql.load(str(html_path), base_dir=str(tmp_path))
    result = markql.execute("SELECT a FROM document")
    assert result.columns
    assert len(result.rows) == 1


def test_execute_with_explicit_doc() -> None:
    doc = markql.load("<html><body><div></div></body></html>")
    result = markql.execute("SELECT div FROM document", doc=doc)
    assert len(result.rows) == 1


def test_execute_examples() -> None:
    doc = markql.load("<html><body><a href='x'></a><a></a></body></html>")
    result = markql.execute("SELECT a.attributes FROM document LIMIT 1", doc=doc)
    assert result.rows[0]["attributes"]["href"] == "x"
    count = markql.execute("SELECT COUNT(a) FROM document", doc=doc)
    assert count.rows[0]["count"] == 2


def test_summarize_output_shape() -> None:
    doc = markql.load("<html><body><div></div></body></html>")
    summary = markql.summarize(doc)
    assert "total_nodes" in summary
    assert "tag_counts" in summary
    assert "attribute_keys" in summary


def test_regex_replace_in_python_execute() -> None:
    doc = markql.load("<html><body><div class='price'> ¥278,120 </div></body></html>")
    result = markql.execute(
        "SELECT REGEX_REPLACE(TRIM(TEXT(div)), '[^0-9]', '') AS digits "
        "FROM document WHERE tag = 'div' AND attributes.class IS NOT NULL",
        doc=doc,
    )
    assert len(result.rows) == 1
    assert result.rows[0]["digits"] == "278120"
