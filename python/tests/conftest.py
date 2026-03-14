import pytest

import markql


@pytest.fixture(scope="session")
def supports_project() -> bool:
    doc = markql.load("<table><tbody><tr><td>2025</td></tr></tbody></table>")
    try:
        markql.execute(
            "SELECT PROJECT(tr) AS (period: TEXT(td WHERE sibling_pos = 1)) "
            "FROM document WHERE EXISTS(child WHERE tag = 'td')",
            doc=doc,
        )
        return True
    except RuntimeError:
        return False


@pytest.fixture(scope="session")
def supports_describe_language() -> bool:
    doc = markql.load("<html><body></body></html>")
    try:
        markql.execute("DESCRIBE language", doc=doc)
        return True
    except RuntimeError:
        return False


@pytest.fixture(scope="session")
def supports_parse_source() -> bool:
    doc = markql.load("<html><body></body></html>")
    try:
        markql.execute(
            "SELECT li FROM PARSE('<ul><li>1</li></ul>') AS frag",
            doc=doc,
        )
        return True
    except RuntimeError:
        return False


@pytest.fixture(scope="session")
def supports_fragments_warnings() -> bool:
    doc = markql.load("<html><body></body></html>")
    try:
        result = markql.execute(
            "SELECT li FROM FRAGMENTS(RAW('<ul><li>1</li></ul>')) AS frag",
            doc=doc,
        )
    except RuntimeError:
        return False
    return bool(result.warnings)
