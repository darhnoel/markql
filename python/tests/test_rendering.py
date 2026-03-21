from pathlib import Path

import pytest

import markql


ROOT = Path(__file__).resolve().parents[2]


def repo_path(relative: str) -> Path:
    return ROOT / relative


def test_python_load_toml_vars_matches_golden_fixture() -> None:
    data = markql.load_toml_vars(repo_path("tests/fixtures/render/golden_query.toml"))
    assert data["row_alias"] == "tr"
    assert data["source"] == "doc"
    assert data["fields"][1] == ["short_name", "companyshortname.raw"]


def test_python_render_j2_query_file_matches_golden_output() -> None:
    rendered = markql.render_j2_query_file(
        repo_path("tests/fixtures/render/golden_query.mql.j2"),
        vars_path=repo_path("tests/fixtures/render/golden_query.toml"),
    )
    expected = repo_path("tests/golden/render/golden_query.mql").read_text(encoding="utf-8")
    assert rendered.text == expected


def test_python_render_j2_query_file_uses_strict_undefined() -> None:
    with pytest.raises(markql.RenderError, match="Missing template variable"):
        markql.render_j2_query_file(repo_path("tests/fixtures/render/golden_query.mql.j2"))


def test_python_load_toml_vars_rejects_invalid_toml() -> None:
    with pytest.raises(markql.RenderError, match="Invalid TOML vars file"):
        markql.load_toml_vars(repo_path("tests/fixtures/render/invalid_vars.toml"))
