from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Any

from .schemas import ArtifactSummary, ModelDecision


def _blank_decision() -> ModelDecision:
    return {
        "status": "blocked",
        "diagnosis": "unknown",
        "reason": "",
        "chosen_family": "",
        "requested_artifact": "",
        "query": "",
        "next_action": "done",
    }


def _pick_family(artifact: ArtifactSummary) -> str:
    for line in artifact["content"].splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if "|" in stripped:
            return stripped.split("|", 1)[0]
    return artifact.get("family_hint", "")


def _row_tag_from_artifact(artifact: ArtifactSummary) -> str:
    for line in artifact["content"].splitlines():
        stripped = line.strip()
        if "|" not in stripped:
            continue
        parts = stripped.split("|")
        if len(parts) < 2:
            continue
        path = parts[1]
        last = path.split(">")[-1].strip()
        if last:
            return re.sub(r"\[.*?\]", "", last)
    return "div"


def _field_aliases(goal_text: str) -> list[str]:
    words = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", goal_text.lower())
    stop = {"extract", "the", "and", "with", "from", "into", "query", "main"}
    aliases: list[str] = []
    for word in words:
        if word in stop:
            continue
        if word not in aliases:
            aliases.append(word)
        if len(aliases) == 3:
            break
    return aliases or ["value"]


def _contains(constraints: list[str], needle: str) -> bool:
    needle_l = needle.lower()
    return any(needle_l in item.lower() for item in constraints)


class HeuristicModelAdapter:
    """Local deterministic fallback for the two bounded helper model tasks."""

    def interpret_and_suggest(self, payload: dict[str, Any]) -> ModelDecision:
        artifact = payload["artifact"]
        constraints = list(payload.get("constraints", []))
        decision = _blank_decision()
        family = _pick_family(artifact)
        row_tag = _row_tag_from_artifact(artifact)
        decision["chosen_family"] = family
        if not artifact.get("content", "").strip():
            decision["status"] = "need_more_artifact"
            decision["diagnosis"] = "artifact_too_lossy"
            decision["reason"] = "artifact is empty; request one richer artifact level"
            decision["requested_artifact"] = "families"
            decision["next_action"] = "escalate"
            return decision

        if _contains(constraints, "use project") and not _contains(constraints, "no project"):
            alias = _field_aliases(payload.get("goal_text", ""))[0]
            decision["query"] = (
                f"SELECT PROJECT({row_tag}) AS ({alias}: TEXT({row_tag}, 1)) "
                f"FROM doc WHERE tag = '{row_tag}' ORDER BY node_id LIMIT 10;"
            )
        elif _contains(constraints, "cte only"):
            decision["query"] = (
                f"WITH r_rows AS (SELECT self.node_id AS row_id FROM doc WHERE tag = '{row_tag}') "
                "SELECT r_row.row_id FROM r_rows AS r_row ORDER BY row_id LIMIT 10;"
            )
        else:
            decision["query"] = (
                f"SELECT self.node_id, self.tag FROM doc WHERE tag = '{row_tag}' "
                "ORDER BY node_id LIMIT 10;"
            )
        decision["status"] = "query_ready"
        decision["diagnosis"] = "row_scope"
        decision["reason"] = "start with a row-check query for one family"
        decision["next_action"] = "lint"
        return decision

    def repair_from_summary(self, payload: dict[str, Any]) -> ModelDecision:
        diagnosis = payload.get("diagnosis", "unknown")
        constraints = list(payload.get("constraints", []))
        current_query = str(payload.get("current_query", "")).strip()
        decision = _blank_decision()
        if diagnosis == "grammar":
            decision["query"] = "SELECT self.node_id FROM doc WHERE tag = 'div' ORDER BY node_id LIMIT 10;"
            decision["diagnosis"] = "grammar"
            decision["reason"] = "replace the failing shape with a valid minimal row-check query"
        elif diagnosis == "field_scope":
            alias = _field_aliases(current_query or "value")[0]
            if _contains(constraints, "no project"):
                decision["query"] = "SELECT TEXT(div) FROM doc WHERE tag = 'div' ORDER BY node_id LIMIT 10;"
            else:
                decision["query"] = (
                    f"SELECT PROJECT(div) AS ({alias}: TEXT(span)) "
                    "FROM doc WHERE tag = 'div' ORDER BY node_id LIMIT 10;"
                )
            decision["diagnosis"] = "field_scope"
            decision["reason"] = "rows exist; repair one supplier field next"
        elif diagnosis == "artifact_too_lossy":
            decision["status"] = "need_more_artifact"
            decision["diagnosis"] = "artifact_too_lossy"
            decision["reason"] = "request one richer artifact level"
            decision["requested_artifact"] = "families"
            decision["next_action"] = "escalate"
            return decision
        else:
            decision["query"] = "SELECT self.node_id, self.tag FROM doc WHERE tag = 'div' ORDER BY node_id LIMIT 10;"
            decision["diagnosis"] = "row_scope"
            decision["reason"] = "repair row scope before attempting richer extraction"
        decision["status"] = "query_ready"
        decision["next_action"] = "lint"
        return decision


@dataclass
class MockModelAdapter:
    """Deterministic test adapter with queued responses per task."""

    suggest_responses: list[ModelDecision] = field(default_factory=list)
    repair_responses: list[ModelDecision] = field(default_factory=list)
    calls: list[tuple[str, dict[str, Any]]] = field(default_factory=list)

    def interpret_and_suggest(self, payload: dict[str, Any]) -> ModelDecision:
        self.calls.append(("interpret_and_suggest", payload))
        if not self.suggest_responses:
            raise AssertionError("No mock interpret_and_suggest response queued")
        return self.suggest_responses.pop(0)

    def repair_from_summary(self, payload: dict[str, Any]) -> ModelDecision:
        self.calls.append(("repair_from_summary", payload))
        if not self.repair_responses:
            raise AssertionError("No mock repair_from_summary response queued")
        return self.repair_responses.pop(0)
