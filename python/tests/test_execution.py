import pathlib

import xsql


def test_execute_uses_doc_fallback(tmp_path: pathlib.Path) -> None:
    html_path = tmp_path / "sample.html"
    html_path.write_text("<html><body><a id='x'></a></body></html>", encoding="utf-8")
    xsql.load(str(html_path), base_dir=str(tmp_path))
    result = xsql.execute("SELECT a FROM document")
    assert result.columns
    assert len(result.rows) == 1


def test_execute_with_explicit_doc() -> None:
    doc = xsql.load("<html><body><div></div></body></html>")
    result = xsql.execute("SELECT div FROM document", doc=doc)
    assert len(result.rows) == 1


def test_execute_examples() -> None:
    doc = xsql.load("<html><body><a href='x'></a><a></a></body></html>")
    result = xsql.execute("SELECT a.attributes FROM document LIMIT 1", doc=doc)
    assert result.rows[0]["attributes"]["href"] == "x"
    count = xsql.execute("SELECT COUNT(a) FROM document", doc=doc)
    assert count.rows[0]["count"] == 2


def test_summarize_output_shape() -> None:
    doc = xsql.load("<html><body><div></div></body></html>")
    summary = xsql.summarize(doc)
    assert "total_nodes" in summary
    assert "tag_counts" in summary
    assert "attribute_keys" in summary
