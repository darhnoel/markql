from __future__ import annotations

import re
import subprocess
from pathlib import Path

from .. import _core, execute, lint_detailed, load
from .._types import QueryResult
from .retrieval_packs import get_retrieval_pack
from .schemas import ArtifactSummary, ExecutionSummary, LintSummary, RetrievalPack, RetrievalTopic


def _read_text(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def _summarize_diagnostics(result: dict) -> LintSummary:
    diagnostics = list(result.get("diagnostics", []))
    summary = dict(result.get("summary", {}))
    first = diagnostics[0] if diagnostics else {}
    category = "none"
    if diagnostics:
        category = "grammar" if first.get("category") == "parse" else "semantic"
    return {
        "ok": summary.get("error_count", 0) == 0,
        "category": category,  # type: ignore[typeddict-item]
        "error_count": int(summary.get("error_count", 0)),
        "warning_count": int(summary.get("warning_count", 0)),
        "headline": str(first.get("message", "")),
        "details": str(first.get("help", "")),
    }


def summarize_result(output: QueryResult, expected_fields: list[str] | None = None) -> ExecutionSummary:
    expected = list(expected_fields or [])
    sample_rows: list[dict[str, str]] = []
    null_field_count = 0
    blank_field_count = 0
    for row in output.rows[:3]:
        rendered: dict[str, str] = {}
        keys = expected or list(row.keys())
        for key in keys:
            value = row.get(key)
            if value is None:
                null_field_count += 1
                rendered[key] = ""
            else:
                text = str(value)
                if text.strip() == "":
                    blank_field_count += 1
                rendered[key] = text
        sample_rows.append(rendered)
    return {
        "ok": True,
        "row_count": len(output.rows),
        "null_field_count": null_field_count,
        "blank_field_count": blank_field_count,
        "has_rows": len(output.rows) > 0,
        "expected_row_count": None,
        "headline": f"{len(output.rows)} rows returned",
        "details": "rows returned from local execution",
        "sample_rows": sample_rows,
    }


class LocalToolAdapters:
    """Stable adapter layer for local helper operations."""

    def __init__(self, inspector_cmd: list[str] | None = None) -> None:
        self._inspector_cmd = inspector_cmd or [
            "cargo",
            "run",
            "--manifest-path",
            "tools/html_inspector/Cargo.toml",
            "--",
        ]

    def _run_inspector(self, flag: str, input_path: str) -> str:
        result = subprocess.run(
            [*self._inspector_cmd, flag, input_path],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
            raise RuntimeError(f"html_inspector failed: {detail}")
        return result.stdout

    def inspect_compact_families(self, input_path: str) -> ArtifactSummary:
        content = self._run_inspector("--families-compact", input_path)
        family_hint = ""
        for line in content.splitlines():
            if "|" in line:
                family_hint = line.split("|", 1)[0].strip()
                break
        return {
            "kind": "compact_families",
            "content": content,
            "selector_or_scope": "",
            "family_hint": family_hint,
            "lossy": True,
            "source": "html_inspector",
        }

    def inspect_families(self, input_path: str) -> ArtifactSummary:
        return {
            "kind": "families",
            "content": self._run_inspector("--families", input_path),
            "selector_or_scope": "",
            "family_hint": "",
            "lossy": True,
            "source": "html_inspector",
        }

    def inspect_skeleton(self, input_path: str) -> ArtifactSummary:
        return {
            "kind": "skeleton",
            "content": self._run_inspector("--skeleton", input_path),
            "selector_or_scope": "",
            "family_hint": "",
            "lossy": True,
            "source": "html_inspector",
        }

    def inspect_targeted_subtree(self, input_path: str, selector_or_scope: str) -> ArtifactSummary:
        html = _read_text(input_path)
        content = html
        if selector_or_scope:
            token = selector_or_scope.strip()
            match = re.search(re.escape(token), html)
            if match is not None:
                start = max(0, match.start() - 1500)
                end = min(len(html), match.end() + 1500)
                content = html[start:end]
        return {
            "kind": "targeted_subtree",
            "content": content,
            "selector_or_scope": selector_or_scope,
            "family_hint": "",
            "lossy": False,
            "source": "slice",
        }

    def retrieve_markql_pack(self, topic: RetrievalTopic) -> RetrievalPack:
        return get_retrieval_pack(topic)

    def lint_markql(self, query: str, input_path: str | None = None) -> LintSummary:
        del input_path
        return _summarize_diagnostics(lint_detailed(query))

    def run_markql(
        self,
        query: str,
        input_path: str,
        expected_fields: list[str] | None = None,
    ) -> ExecutionSummary:
        doc = load(input_path)
        output = execute(query, doc=doc)
        return summarize_result(output, expected_fields=expected_fields)


def inspect_with_requested_artifact(
    adapters: LocalToolAdapters,
    requested_artifact: str,
    input_path: str,
    selector_or_scope: str = "",
) -> ArtifactSummary:
    if requested_artifact == "compact_families":
        return adapters.inspect_compact_families(input_path)
    if requested_artifact == "families":
        return adapters.inspect_families(input_path)
    if requested_artifact == "skeleton":
        return adapters.inspect_skeleton(input_path)
    if requested_artifact == "targeted_subtree":
        return adapters.inspect_targeted_subtree(input_path, selector_or_scope)
    if requested_artifact == "full_html":
        return {
            "kind": "full_html",
            "content": _read_text(input_path),
            "selector_or_scope": selector_or_scope,
            "family_hint": "",
            "lossy": False,
            "source": "raw",
        }
    raise ValueError(f"Unsupported helper artifact request: {requested_artifact}")
