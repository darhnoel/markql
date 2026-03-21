"""Helpers for rendering templated query files into plain MarkQL text."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

import jinja2

try:  # pragma: no cover - exercised on Python < 3.11 only.
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - exercised on Python < 3.11 only.
    import tomli as tomllib


class RenderError(ValueError):
    """Raised when template vars loading or rendering fails."""


@dataclass(frozen=True)
class RenderedQuery:
    text: str
    template_path: str
    vars_path: str | None = None


def load_toml_vars(path: str | Path) -> dict[str, Any]:
    file_path = Path(path)
    try:
        raw = file_path.read_bytes()
    except OSError as exc:
        raise RenderError(f"Failed to open vars file: {file_path}") from exc

    try:
        data = tomllib.loads(raw.decode("utf-8"))
    except UnicodeDecodeError as exc:
        raise RenderError(f"Vars file is not valid UTF-8: {file_path}") from exc
    except tomllib.TOMLDecodeError as exc:
        raise RenderError(f"Invalid TOML vars file: {file_path}: {exc}") from exc

    if not isinstance(data, dict):
        raise RenderError(f"TOML vars file must decode to a mapping: {file_path}")
    return dict(data)


def render_j2_template_text(
    template_text: str,
    *,
    template_name: str,
    variables: Mapping[str, Any],
) -> str:
    environment = jinja2.Environment(
        autoescape=False,
        keep_trailing_newline=True,
        undefined=jinja2.StrictUndefined,
    )
    try:
        template = environment.from_string(template_text)
    except jinja2.TemplateSyntaxError as exc:
        raise RenderError(
            f"Invalid Jinja2 template syntax in {template_name} at line {exc.lineno}: {exc.message}"
        ) from exc

    try:
        return template.render(**dict(variables))
    except jinja2.UndefinedError as exc:
        raise RenderError(f"Missing template variable while rendering {template_name}: {exc}") from exc
    except jinja2.TemplateError as exc:
        raise RenderError(f"Failed to render Jinja2 template {template_name}: {exc}") from exc


def render_j2_query_file(
    template_path: str | Path,
    *,
    vars_path: str | Path | None = None,
) -> RenderedQuery:
    template_file = Path(template_path)
    try:
        template_text = template_file.read_text(encoding="utf-8")
    except UnicodeDecodeError as exc:
        raise RenderError(f"Template file is not valid UTF-8: {template_file}") from exc
    except OSError as exc:
        raise RenderError(f"Failed to open template file: {template_file}") from exc

    variables = load_toml_vars(vars_path) if vars_path is not None else {}
    rendered = render_j2_template_text(
        template_text,
        template_name=str(template_file),
        variables=variables,
    )
    return RenderedQuery(
        text=rendered,
        template_path=str(template_file),
        vars_path=str(vars_path) if vars_path is not None else None,
    )
