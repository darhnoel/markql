import markql


def test_python_lint_returns_structured_diagnostics() -> None:
    diagnostics = markql.lint("SELECT FROM doc")
    assert diagnostics
    first = diagnostics[0]
    assert first["severity"] == "ERROR"
    assert first["code"].startswith("MQL-SYN-")
    assert first["category"] == "parse"
    assert first["message"] == "Missing projection after SELECT"
    assert "help" in first and first["help"]
    assert "why" in first and first["why"]
    assert "example" in first and first["example"]
    assert first["expected"] == "projection after SELECT"
    assert first["encountered"] == "FROM"
    assert "doc_ref" in first and first["doc_ref"]
    assert "snippet" in first and "^" in first["snippet"]


def test_python_lint_detailed_reports_coverage() -> None:
    result = markql.lint_detailed(
        "WITH r AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') "
        "SELECT x.row_id FROM r AS x WHERE x.tagy = 'tr'"
    )
    summary = result["summary"]
    diagnostics = result["diagnostics"]
    assert summary["parse_succeeded"] is True
    assert summary["coverage"] == "reduced"
    assert summary["used_reduced_validation"] is True
    assert summary["warning_count"] >= 1
    assert diagnostics
    assert diagnostics[0]["code"] == "MQL-LINT-0002"


def test_python_exposes_core_version_provenance() -> None:
    info = markql.core_version_info()
    assert "version" in info and isinstance(info["version"], str) and info["version"]
    assert "git_commit" in info and isinstance(info["git_commit"], str)
    assert "git_dirty" in info
    assert "provenance" in info and isinstance(info["provenance"], str)
    assert markql.__version__ == info["version"]
