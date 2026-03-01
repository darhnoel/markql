import pytest

import xsql


@pytest.fixture(scope="session")
def supports_project() -> bool:
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


@pytest.fixture(scope="session")
def supports_describe_language() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        xsql.execute("DESCRIBE language", doc=doc)
        return True
    except RuntimeError:
        return False


@pytest.fixture(scope="session")
def supports_parse_source() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        xsql.execute(
            "SELECT li FROM PARSE('<ul><li>1</li></ul>') AS frag",
            doc=doc,
        )
        return True
    except RuntimeError:
        return False


@pytest.fixture(scope="session")
def supports_fragments_warnings() -> bool:
    doc = xsql.load("<html><body></body></html>")
    try:
        result = xsql.execute(
            "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li></ul>')) AS frag",
            doc=doc,
        )
    except RuntimeError:
        return False
    return bool(result.warnings)
