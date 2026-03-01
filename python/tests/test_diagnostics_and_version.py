import xsql


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
