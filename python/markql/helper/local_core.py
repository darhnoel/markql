from __future__ import annotations

from .schemas import (
    ArtifactSummary,
    ControllerSnapshot,
    ControllerStep,
    ExecutionSummary,
    FinalSuggestion,
    HelperRequest,
    LintSummary,
    RetrievalPack,
    RetrievalTopic,
    ResultAnalysis,
)


def helper_retrieval_pack(topic: RetrievalTopic) -> RetrievalPack:
    packs: dict[str, RetrievalPack] = {
        "row_selection": {
            "topic": "row_selection",
            "summary": "Outer WHERE controls row survival; use EXISTS(...) when supplier existence should decide row inclusion.",
            "facts": [
                "MarkQL runs in two stages: outer WHERE keeps rows and field expressions choose values.",
                "Use EXISTS(child|descendant ...) in outer WHERE to gate rows before field extraction.",
            ],
            "examples": [
                "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(child WHERE tag = 'h3') ORDER BY node_id;"
            ],
            "doc_refs": ["docs/book/ch02-mental-model.md", "docs/book/ch03-first-query-loop.md"],
        },
        "exploration": {
            "topic": "exploration",
            "summary": "Use small exploratory queries first: inspect rows, then test one field.",
            "facts": [
                "Start with one family and one row-check query.",
                "FLATTEN/TEXT/ATTR are for inspection; do not jump to a large extraction first.",
            ],
            "examples": ["SELECT a.href, a.tag FROM doc WHERE href IS NOT NULL TO LIST();"],
            "doc_refs": [
                "docs/book/ch03-first-query-loop.md",
                "tools/html_inspector/docs/ai_inspection_playbook.md",
            ],
        },
        "stabilization": {
            "topic": "stabilization",
            "summary": "When the query stops being trivial, stabilize row scope with helper relations before scaling extraction.",
            "facts": [
                "Use WITH row-anchor relations for stable row ids.",
                "Keep helper-row CTEs separate from pure shaped PROJECT/FLATTEN CTEs.",
            ],
            "examples": [
                "WITH r_rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') SELECT r_row.row_id FROM r_rows AS r_row;"
            ],
            "doc_refs": [
                "docs/book/ch03-first-query-loop.md",
                "tools/html_inspector/docs/ai_markql_musts.md",
            ],
        },
        "stable_extraction": {
            "topic": "stable_extraction",
            "summary": "Once row scope is proven, use PROJECT, COALESCE, and positional extraction for stable fields.",
            "facts": [
                "PROJECT(base_tag) AS (...) evaluates fields per kept row.",
                "TEXT(..., n), FIRST_TEXT(...), and LAST_TEXT(...) are selector-position tools, not row selectors.",
            ],
            "examples": [
                "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), second_span: TEXT(span, 2), last_span: LAST_TEXT(span)) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;"
            ],
            "doc_refs": ["docs/book/ch09-project.md", "docs/markql-cli-guide.md"],
        },
        "repair": {
            "topic": "repair",
            "summary": "Repair the smallest failing shape first and feed back only a short lint or execution summary.",
            "facts": [
                "Lint first, then execute only if the query shape is valid.",
                "Wrong rows means row-scope repair; null fields on right rows means supplier repair.",
            ],
            "examples": [
                "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')) FROM doc WHERE tag='section' AND EXISTS(descendant WHERE tag='span' AND text LIKE '%stop%') ORDER BY node_id;"
            ],
            "doc_refs": [
                "docs/book/ch12-troubleshooting.md",
                "tools/html_inspector/docs/ai_markql_musts.md",
            ],
        },
        "grammar": {
            "topic": "grammar",
            "summary": "Use only verified grammar and clause order from the current build.",
            "facts": [
                "Do not invent unsupported axes, operators, or top-level extraction forms.",
                "Clause order and PROJECT/FLATTEN shapes are fixed by the current grammar.",
            ],
            "examples": ["SELECT li.node_id, PROJECT(li) AS (title: TEXT(h2)) FROM doc WHERE tag = 'li';"],
            "doc_refs": [
                "docs/book/appendix-grammar.md",
                "docs/book/appendix-function-reference.md",
            ],
        },
        "null_and_scope": {
            "topic": "null_and_scope",
            "summary": "NULL is a field-level signal unless stage 1 dropped the row.",
            "facts": [
                "Correct rows with NULL fields usually mean supplier logic is wrong.",
                "Field predicates pick suppliers; they do not remove rows that already survived outer WHERE.",
            ],
            "examples": [
                "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')) FROM doc WHERE tag = 'section' ORDER BY node_id;"
            ],
            "doc_refs": ["docs/book/ch02-mental-model.md", "docs/book/ch10-null-and-stability.md"],
        },
    }
    return packs[topic]


def _choose_retrieval_topic(snapshot: ControllerSnapshot) -> RetrievalTopic:
    lint = snapshot.get("lint_summary")
    if lint and not lint["ok"]:
        return "grammar" if lint["category"] == "grammar" else "repair"
    analysis = snapshot.get("result_analysis")
    if analysis:
        if analysis["diagnosis"] == "grammar":
            return "grammar"
        if analysis["diagnosis"] == "semantic":
            return "repair"
        if analysis["diagnosis"] == "row_scope":
            return "stabilization" if snapshot.get("current_query") else "row_selection"
        if analysis["diagnosis"] == "field_scope":
            return "stable_extraction" if any(
                "project" in item.lower() for item in snapshot["request"]["constraints"]
            ) else "null_and_scope"
        if analysis["diagnosis"] == "artifact_too_lossy":
            return "exploration"
        if analysis["diagnosis"] == "mixed":
            return "repair"
    constraints = snapshot["request"]["constraints"]
    if any("no project" in item.lower() for item in constraints):
        return "exploration"
    if any("use project" in item.lower() for item in constraints):
        return "stable_extraction"
    if snapshot.get("current_query"):
        return "repair"
    return "row_selection"


def helper_analyze_result(
    request: HelperRequest,
    artifact: ArtifactSummary,
    lint_summary: LintSummary | None,
    execution_summary: ExecutionSummary | None,
) -> ResultAnalysis:
    del request
    if lint_summary is not None and not lint_summary["ok"]:
        if lint_summary["category"] == "grammar":
            return {
                "category": "grammar_failure",
                "diagnosis": "grammar",
                "reason": lint_summary["headline"] or "grammar failure during lint",
                "should_repair": True,
                "should_escalate": False,
                "should_execute": False,
                "done": False,
            }
        return {
            "category": "semantic_failure",
            "diagnosis": "semantic",
            "reason": lint_summary["headline"] or "semantic or lint failure during lint",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if execution_summary is None:
        return {
            "category": "likely_success",
            "diagnosis": "none",
            "reason": "no execution summary available yet",
            "should_repair": False,
            "should_escalate": False,
            "should_execute": True,
            "done": False,
        }
    if not execution_summary["ok"]:
        return {
            "category": "semantic_failure",
            "diagnosis": "semantic",
            "reason": execution_summary["headline"] or "execution failed",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if artifact["lossy"] and not execution_summary["has_rows"] and artifact["kind"] != "full_html":
        return {
            "category": "artifact_too_lossy",
            "diagnosis": "artifact_too_lossy",
            "reason": "current artifact is too lossy to explain the empty result safely",
            "should_repair": False,
            "should_escalate": True,
            "should_execute": False,
            "done": False,
        }
    if execution_summary["expected_row_count"] is not None and execution_summary["row_count"] != execution_summary["expected_row_count"]:
        return {
            "category": "wrong_row_count",
            "diagnosis": "row_scope",
            "reason": "row count does not match expectation; repair row scope first",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if not execution_summary["has_rows"] or execution_summary["row_count"] == 0:
        return {
            "category": "empty_output",
            "diagnosis": "row_scope",
            "reason": "query returned no rows; repair row scope before field logic",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if execution_summary["null_field_count"] > 0 and execution_summary["blank_field_count"] > 0:
        return {
            "category": "mixed_instability",
            "diagnosis": "mixed",
            "reason": "rows exist but field output is mixed; supplier logic or artifact detail is unstable",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if execution_summary["null_field_count"] > 0:
        return {
            "category": "right_rows_null_fields",
            "diagnosis": "field_scope",
            "reason": "rows look present but fields are NULL; repair supplier logic",
            "should_repair": True,
            "should_escalate": False,
            "should_execute": False,
            "done": False,
        }
    if execution_summary["blank_field_count"] > 0 and artifact["lossy"] and artifact["kind"] != "full_html":
        return {
            "category": "artifact_too_lossy",
            "diagnosis": "artifact_too_lossy",
            "reason": "blank fields on a lossy artifact suggest one-step artifact escalation",
            "should_repair": False,
            "should_escalate": True,
            "should_execute": False,
            "done": False,
        }
    return {
        "category": "likely_success",
        "diagnosis": "none",
        "reason": "query looks like a good next step",
        "should_repair": False,
        "should_escalate": False,
        "should_execute": False,
        "done": True,
    }


def _final_suggestion(snapshot: ControllerSnapshot, status: str, reason: str, diagnosis: str) -> FinalSuggestion:
    return {
        "status": status,  # type: ignore[typeddict-item]
        "mode": snapshot["request"]["mode"],
        "query": snapshot.get("current_query", "") or snapshot.get("model_decision", {}).get("query", ""),
        "diagnosis": diagnosis,  # type: ignore[typeddict-item]
        "reason": reason,
        "artifact_used": snapshot.get("artifact", {}).get("kind", ""),  # type: ignore[typeddict-item]
        "retrieval_topic": snapshot.get("retrieval_pack", {}).get("topic", _choose_retrieval_topic(snapshot)),  # type: ignore[typeddict-item]
        "lint_summary": snapshot.get(
            "lint_summary",
            {"ok": True, "category": "none", "error_count": 0, "warning_count": 0, "headline": "", "details": ""},
        ),
        "execution_summary": snapshot.get(
            "execution_summary",
            {
                "ok": False,
                "row_count": 0,
                "null_field_count": 0,
                "blank_field_count": 0,
                "has_rows": False,
                "expected_row_count": None,
                "headline": "",
                "details": "",
                "sample_rows": [],
            },
        ),
    }


def helper_plan_next(snapshot: ControllerSnapshot) -> ControllerStep:
    if not snapshot.get("artifact"):
        return {
            "state": "INSPECT_COMPACT",
            "action": "inspect_artifact",
            "requested_artifact": "compact_families",
            "requested_pack": "row_selection",
            "model_task": "none",
            "diagnosis": "none",
            "reason": "start from compact families on the common path",
            "query": "",
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    if not snapshot.get("retrieval_pack"):
        return {
            "state": "RETRIEVE_PACK",
            "action": "build_retrieval_pack",
            "requested_artifact": "",
            "requested_pack": _choose_retrieval_topic(snapshot),
            "model_task": "none",
            "diagnosis": "none",
            "reason": "build one tiny retrieval pack for the current step",
            "query": "",
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    if not snapshot.get("current_query") and not snapshot.get("model_decision"):
        return {
            "state": "MODEL_SUGGEST",
            "action": "call_model",
            "requested_artifact": "",
            "requested_pack": snapshot["retrieval_pack"]["topic"],
            "model_task": "interpret_and_suggest",
            "diagnosis": "none",
            "reason": "need one next query suggestion from the current goal and artifact",
            "query": "",
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    decision = snapshot.get("model_decision")
    if decision and not snapshot.get("current_query"):
        if decision["status"] == "need_more_artifact":
            current = snapshot["artifact"]["kind"]
            next_kind = {
                "compact_families": "families",
                "families": "skeleton",
                "skeleton": "targeted_subtree",
                "targeted_subtree": "full_html",
                "full_html": "full_html",
            }[current]
            if current == "full_html":
                return {
                    "state": "BLOCKED",
                    "action": "blocked",
                    "requested_artifact": "",
                    "requested_pack": snapshot["retrieval_pack"]["topic"],
                    "model_task": "none",
                    "diagnosis": "artifact_too_lossy",
                    "reason": "model requested more artifact detail but no further escalation is available",
                    "query": "",
                    "final_suggestion": _final_suggestion(
                        snapshot,
                        "blocked",
                        "model requested more artifact detail but no further escalation is available",
                        "artifact_too_lossy",
                    ),
                }
            return {
                "state": "ESCALATE_ARTIFACT",
                "action": "escalate_artifact",
                "requested_artifact": decision["requested_artifact"] or next_kind,  # type: ignore[typeddict-item]
                "requested_pack": snapshot["retrieval_pack"]["topic"],
                "model_task": "none",
                "diagnosis": decision["diagnosis"],
                "reason": "escalate one artifact level only",
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
            }
        if decision["status"] == "blocked":
            return {
                "state": "BLOCKED",
                "action": "blocked",
                "requested_artifact": "",
                "requested_pack": snapshot["retrieval_pack"]["topic"],
                "model_task": "none",
                "diagnosis": decision["diagnosis"],
                "reason": decision["reason"],
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", decision["reason"], decision["diagnosis"]),
            }
        if decision["status"] == "query_ready" and decision["query"]:
            return {
                "state": "LINT_QUERY",
                "action": "lint_query",
                "requested_artifact": "",
                "requested_pack": snapshot["retrieval_pack"]["topic"],
                "model_task": "none",
                "diagnosis": decision["diagnosis"],
                "reason": "lint the single suggested query before execution",
                "query": decision["query"],
                "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
            }
    if snapshot.get("current_query") and not snapshot.get("lint_summary"):
        return {
            "state": "LINT_QUERY",
            "action": "lint_query",
            "requested_artifact": "",
            "requested_pack": snapshot["retrieval_pack"]["topic"],
            "model_task": "none",
            "diagnosis": "none",
            "reason": "lint the current query before execution",
            "query": snapshot["current_query"],
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    lint = snapshot.get("lint_summary")
    if lint and not lint["ok"]:
        desired_pack = _choose_retrieval_topic(snapshot)
        if snapshot["retrieval_pack"]["topic"] != desired_pack:
            return {
                "state": "RETRIEVE_PACK",
                "action": "build_retrieval_pack",
                "requested_artifact": "",
                "requested_pack": desired_pack,
                "model_task": "none",
                "diagnosis": lint["category"],
                "reason": "refresh the retrieval pack for the current lint failure",
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
            }
        if snapshot.get("repair_loops", 0) >= 2:
            return {
                "state": "BLOCKED",
                "action": "blocked",
                "requested_artifact": "",
                "requested_pack": desired_pack,
                "model_task": "none",
                "diagnosis": lint["category"],
                "reason": "maximum repair loops reached after lint failure",
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", "maximum repair loops reached after lint failure", lint["category"]),
            }
        return {
            "state": "MODEL_REPAIR",
            "action": "call_model",
            "requested_artifact": "",
            "requested_pack": desired_pack,
            "model_task": "repair_from_summary",
            "diagnosis": lint["category"],
            "reason": "repair the query shape from a short lint summary",
            "query": "",
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    if lint and lint["ok"] and not snapshot.get("execution_summary"):
        return {
            "state": "EXECUTE_QUERY",
            "action": "execute_query",
            "requested_artifact": "",
            "requested_pack": snapshot["retrieval_pack"]["topic"],
            "model_task": "none",
            "diagnosis": "none",
            "reason": "execute the lint-clean query to classify the next step",
            "query": snapshot["current_query"],
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    if snapshot.get("execution_summary") and not snapshot.get("result_analysis"):
        return {
            "state": "ANALYZE_RESULT",
            "action": "analyze_result",
            "requested_artifact": "",
            "requested_pack": snapshot["retrieval_pack"]["topic"],
            "model_task": "none",
            "diagnosis": "none",
            "reason": "classify row scope vs field scope before any repair",
            "query": "",
            "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
        }
    analysis = snapshot.get("result_analysis")
    if analysis:
        if analysis["done"]:
            return {
                "state": "DONE",
                "action": "done",
                "requested_artifact": "",
                "requested_pack": snapshot["retrieval_pack"]["topic"],
                "model_task": "none",
                "diagnosis": analysis["diagnosis"],
                "reason": analysis["reason"],
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "done", analysis["reason"], analysis["diagnosis"]),
            }
        if analysis["should_escalate"]:
            current = snapshot["artifact"]["kind"]
            if current == "full_html":
                return {
                    "state": "BLOCKED",
                    "action": "blocked",
                    "requested_artifact": "",
                    "requested_pack": snapshot["retrieval_pack"]["topic"],
                    "model_task": "none",
                    "diagnosis": "artifact_too_lossy",
                    "reason": "artifact escalation requested but no further artifact level is available",
                    "query": "",
                    "final_suggestion": _final_suggestion(snapshot, "blocked", "artifact escalation requested but no further artifact level is available", "artifact_too_lossy"),
                }
            next_kind = {
                "compact_families": "families",
                "families": "skeleton",
                "skeleton": "targeted_subtree",
                "targeted_subtree": "full_html",
                "full_html": "full_html",
            }[current]
            return {
                "state": "ESCALATE_ARTIFACT",
                "action": "escalate_artifact",
                "requested_artifact": next_kind,  # type: ignore[typeddict-item]
                "requested_pack": snapshot["retrieval_pack"]["topic"],
                "model_task": "none",
                "diagnosis": analysis["diagnosis"],
                "reason": analysis["reason"],
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
            }
        if analysis["should_repair"]:
            desired_pack = _choose_retrieval_topic(snapshot)
            if snapshot["retrieval_pack"]["topic"] != desired_pack:
                return {
                    "state": "RETRIEVE_PACK",
                    "action": "build_retrieval_pack",
                    "requested_artifact": "",
                    "requested_pack": desired_pack,
                    "model_task": "none",
                    "diagnosis": analysis["diagnosis"],
                    "reason": "refresh the retrieval pack for the current diagnosis",
                    "query": "",
                    "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
                }
            if snapshot.get("repair_loops", 0) >= 2:
                return {
                    "state": "BLOCKED",
                    "action": "blocked",
                    "requested_artifact": "",
                    "requested_pack": desired_pack,
                    "model_task": "none",
                    "diagnosis": analysis["diagnosis"],
                    "reason": "maximum repair loops reached after deterministic analysis",
                    "query": "",
                    "final_suggestion": _final_suggestion(snapshot, "blocked", "maximum repair loops reached after deterministic analysis", analysis["diagnosis"]),
                }
            return {
                "state": "MODEL_REPAIR",
                "action": "call_model",
                "requested_artifact": "",
                "requested_pack": desired_pack,
                "model_task": "repair_from_summary",
                "diagnosis": analysis["diagnosis"],
                "reason": analysis["reason"],
                "query": "",
                "final_suggestion": _final_suggestion(snapshot, "blocked", "", "unknown"),
            }
    return {
        "state": "BLOCKED",
        "action": "blocked",
        "requested_artifact": "",
        "requested_pack": snapshot.get("retrieval_pack", {}).get("topic", "row_selection"),
        "model_task": "none",
        "diagnosis": "unknown",
        "reason": "controller could not determine the next bounded step",
        "query": "",
        "final_suggestion": _final_suggestion(snapshot, "blocked", "controller could not determine the next bounded step", "unknown"),
    }
