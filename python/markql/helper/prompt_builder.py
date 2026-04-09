from __future__ import annotations

import json
from typing import Any

from .schemas import ArtifactSummary, ExecutionSummary, LintSummary, RetrievalPack


def build_interpret_payload(
    *,
    goal_text: str,
    artifact: ArtifactSummary,
    constraints: list[str],
    retrieval_pack: RetrievalPack,
    current_query: str = "",
    lint_summary: LintSummary | None = None,
    execution_summary: ExecutionSummary | None = None,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "task": "interpret_and_suggest",
        "goal_text": goal_text,
        "artifact": artifact,
        "constraints": constraints,
        "retrieval_pack": retrieval_pack,
        "current_query": current_query,
    }
    if lint_summary is not None:
        payload["lint_summary"] = lint_summary
    if execution_summary is not None:
        payload["execution_summary"] = execution_summary
    return payload


def build_repair_payload(
    *,
    current_query: str,
    diagnosis: str,
    retrieval_pack: RetrievalPack,
    constraints: list[str],
    lint_summary: LintSummary | None = None,
    execution_summary: ExecutionSummary | None = None,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "task": "repair_from_summary",
        "current_query": current_query,
        "diagnosis": diagnosis,
        "constraints": constraints,
        "retrieval_pack": retrieval_pack,
    }
    if lint_summary is not None:
        payload["lint_summary"] = lint_summary
    if execution_summary is not None:
        payload["execution_summary"] = execution_summary
    return payload


def render_prompt_json(payload: dict[str, Any]) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
