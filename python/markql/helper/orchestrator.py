from __future__ import annotations

from typing import Any

from .. import _core
from . import local_core
from .model_adapter import HeuristicModelAdapter
from .prompt_builder import build_interpret_payload, build_repair_payload
from .schemas import ControllerSnapshot, FinalSuggestion, HelperRequest, ModelAdapter, ToolAdapters
from .tool_adapters import LocalToolAdapters, inspect_with_requested_artifact


def _require_helper_core() -> None:
    return None


def _request_dict(
    *,
    mode: str,
    input_path: str,
    goal_text: str,
    query: str = "",
    constraints: list[str] | None = None,
    expected_fields: list[str] | None = None,
) -> HelperRequest:
    return {
        "mode": mode,  # type: ignore[typeddict-item]
        "input_path": input_path,
        "goal_text": goal_text,
        "query": query,
        "constraints": list(constraints or []),
        "expected_fields": list(expected_fields or []),
    }


class HelperOrchestrator:
    def __init__(
        self,
        *,
        tool_adapters: ToolAdapters | None = None,
        model_adapter: ModelAdapter | None = None,
        max_steps: int = 12,
    ) -> None:
        self.tool_adapters = tool_adapters or LocalToolAdapters()
        self.model_adapter = model_adapter or HeuristicModelAdapter()
        self.max_steps = max_steps

    def run(
        self,
        *,
        mode: str,
        input_path: str,
        goal_text: str,
        query: str = "",
        constraints: list[str] | None = None,
        expected_fields: list[str] | None = None,
    ) -> FinalSuggestion:
        _require_helper_core()
        request = _request_dict(
            mode=mode,
            input_path=input_path,
            goal_text=goal_text,
            query=query,
            constraints=constraints,
            expected_fields=expected_fields,
        )
        snapshot: ControllerSnapshot = {
            "request": request,
            "state": "START",
            "current_query": query,
            "repair_loops": 0,
        }
        for _ in range(self.max_steps):
            if _core is not None and hasattr(_core, "helper_plan_next"):
                step = dict(_core.helper_plan_next(snapshot))
            else:
                step = local_core.helper_plan_next(snapshot)
            action = step["action"]
            if action == "inspect_artifact":
                snapshot["artifact"] = inspect_with_requested_artifact(
                    self.tool_adapters,
                    step["requested_artifact"],
                    input_path,
                )
                snapshot["state"] = step["state"]
                snapshot.pop("retrieval_pack", None)
                snapshot.pop("model_decision", None)
                continue
            if action == "build_retrieval_pack":
                snapshot["retrieval_pack"] = self.tool_adapters.retrieve_markql_pack(step["requested_pack"])
                snapshot["state"] = step["state"]
                continue
            if action == "call_model":
                retrieval_pack = snapshot["retrieval_pack"]
                if step["model_task"] == "interpret_and_suggest":
                    payload = build_interpret_payload(
                        goal_text=goal_text,
                        artifact=snapshot["artifact"],
                        constraints=request["constraints"],
                        retrieval_pack=retrieval_pack,
                        current_query=snapshot.get("current_query", ""),
                        lint_summary=snapshot.get("lint_summary"),
                        execution_summary=snapshot.get("execution_summary"),
                    )
                    decision = self.model_adapter.interpret_and_suggest(payload)
                else:
                    payload = build_repair_payload(
                        current_query=snapshot.get("current_query", ""),
                        diagnosis=step["diagnosis"],
                        retrieval_pack=retrieval_pack,
                        constraints=request["constraints"],
                        lint_summary=snapshot.get("lint_summary"),
                        execution_summary=snapshot.get("execution_summary"),
                    )
                    decision = self.model_adapter.repair_from_summary(payload)
                    snapshot["repair_loops"] = snapshot.get("repair_loops", 0) + 1
                snapshot["model_decision"] = decision
                snapshot["state"] = step["state"]
                if decision["status"] == "query_ready" and decision["query"]:
                    snapshot["current_query"] = decision["query"]
                    snapshot.pop("lint_summary", None)
                    snapshot.pop("execution_summary", None)
                    snapshot.pop("result_analysis", None)
                continue
            if action == "lint_query":
                query_text = step["query"] or snapshot.get("current_query", "")
                snapshot["current_query"] = query_text
                snapshot["lint_summary"] = self.tool_adapters.lint_markql(query_text, input_path)
                snapshot["state"] = step["state"]
                snapshot.pop("execution_summary", None)
                snapshot.pop("result_analysis", None)
                snapshot.pop("model_decision", None)
                continue
            if action == "execute_query":
                query_text = step["query"] or snapshot.get("current_query", "")
                snapshot["execution_summary"] = self.tool_adapters.run_markql(
                    query_text,
                    input_path,
                    expected_fields=request["expected_fields"],
                )
                snapshot["state"] = step["state"]
                snapshot.pop("result_analysis", None)
                continue
            if action == "analyze_result":
                if _core is not None and hasattr(_core, "helper_analyze_result"):
                    snapshot["result_analysis"] = dict(
                        _core.helper_analyze_result(
                            request,
                            snapshot["artifact"],
                            snapshot.get("lint_summary"),
                            snapshot.get("execution_summary"),
                        )
                    )
                else:
                    snapshot["result_analysis"] = local_core.helper_analyze_result(
                        request,
                        snapshot["artifact"],
                        snapshot.get("lint_summary"),
                        snapshot.get("execution_summary"),
                    )
                snapshot["state"] = step["state"]
                continue
            if action == "escalate_artifact":
                selector = snapshot.get("artifact", {}).get("selector_or_scope", "")
                if "model_decision" in snapshot and snapshot["model_decision"].get("chosen_family"):
                    selector = snapshot["model_decision"]["chosen_family"]
                snapshot["artifact"] = inspect_with_requested_artifact(
                    self.tool_adapters,
                    step["requested_artifact"],
                    input_path,
                    selector_or_scope=selector,
                )
                snapshot["state"] = step["state"]
                snapshot.pop("retrieval_pack", None)
                snapshot.pop("result_analysis", None)
                snapshot.pop("model_decision", None)
                continue
            if action in {"done", "blocked"}:
                final = dict(step["final_suggestion"])
                if not final.get("query"):
                    final["query"] = snapshot.get("current_query", "")
                return final  # type: ignore[return-value]
            raise RuntimeError(f"Unsupported helper controller action: {action}")
        return {
            "status": "blocked",
            "mode": mode,  # type: ignore[typeddict-item]
            "query": snapshot.get("current_query", ""),
            "diagnosis": "unknown",
            "reason": "helper exceeded bounded step limit",
            "artifact_used": snapshot.get("artifact", {}).get("kind", ""),
            "retrieval_topic": snapshot.get("retrieval_pack", {}).get("topic", "row_selection"),
            "lint_summary": snapshot.get(
                "lint_summary",
                {
                    "ok": True,
                    "category": "none",
                    "error_count": 0,
                    "warning_count": 0,
                    "headline": "",
                    "details": "",
                },
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


def suggest_query(
    *,
    input_path: str,
    goal_text: str,
    constraints: list[str] | None = None,
    expected_fields: list[str] | None = None,
    tool_adapters: ToolAdapters | None = None,
    model_adapter: ModelAdapter | None = None,
) -> FinalSuggestion:
    return HelperOrchestrator(tool_adapters=tool_adapters, model_adapter=model_adapter).run(
        mode="start",
        input_path=input_path,
        goal_text=goal_text,
        constraints=constraints,
        expected_fields=expected_fields,
    )


def repair_query(
    *,
    input_path: str,
    goal_text: str,
    query: str,
    constraints: list[str] | None = None,
    expected_fields: list[str] | None = None,
    tool_adapters: ToolAdapters | None = None,
    model_adapter: ModelAdapter | None = None,
) -> FinalSuggestion:
    return HelperOrchestrator(tool_adapters=tool_adapters, model_adapter=model_adapter).run(
        mode="repair",
        input_path=input_path,
        goal_text=goal_text,
        query=query,
        constraints=constraints,
        expected_fields=expected_fields,
    )


def explain_query(
    *,
    input_path: str,
    goal_text: str,
    query: str,
    constraints: list[str] | None = None,
    expected_fields: list[str] | None = None,
    tool_adapters: ToolAdapters | None = None,
    model_adapter: ModelAdapter | None = None,
) -> FinalSuggestion:
    return HelperOrchestrator(tool_adapters=tool_adapters, model_adapter=model_adapter).run(
        mode="explain",
        input_path=input_path,
        goal_text=goal_text,
        query=query,
        constraints=constraints,
        expected_fields=expected_fields,
    )
