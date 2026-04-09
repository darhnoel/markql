from __future__ import annotations

from dataclasses import dataclass, field

import markql
from markql.helper import HeuristicModelAdapter, MockModelAdapter
from markql.helper.orchestrator import explain_query, repair_query, suggest_query
from markql.helper.prompt_builder import build_interpret_payload, build_repair_payload
from markql.helper.retrieval_packs import get_retrieval_pack


@dataclass
class FakeAdapters:
    artifacts: dict[str, dict]
    lint_summary: dict
    execution_summary: dict
    inspect_calls: list[str] = field(default_factory=list)
    lint_calls: list[str] = field(default_factory=list)
    execute_calls: list[str] = field(default_factory=list)

    def inspect_compact_families(self, input_path: str) -> dict:
        del input_path
        self.inspect_calls.append("compact_families")
        return self.artifacts["compact_families"]

    def inspect_families(self, input_path: str) -> dict:
        del input_path
        self.inspect_calls.append("families")
        return self.artifacts["families"]

    def inspect_skeleton(self, input_path: str) -> dict:
        del input_path
        self.inspect_calls.append("skeleton")
        return self.artifacts["skeleton"]

    def inspect_targeted_subtree(self, input_path: str, selector_or_scope: str) -> dict:
        del input_path, selector_or_scope
        self.inspect_calls.append("targeted_subtree")
        return self.artifacts["targeted_subtree"]

    def retrieve_markql_pack(self, topic: str) -> dict:
        return get_retrieval_pack(topic)  # type: ignore[arg-type]

    def lint_markql(self, query: str, input_path: str | None = None) -> dict:
        del input_path
        self.lint_calls.append(query)
        return self.lint_summary

    def run_markql(self, query: str, input_path: str, expected_fields: list[str] | None = None) -> dict:
        del input_path, expected_fields
        self.execute_calls.append(query)
        return self.execution_summary


def _artifact(kind: str, content: str, lossy: bool) -> dict:
    return {
        "kind": kind,
        "content": content,
        "selector_or_scope": "",
        "family_hint": "D1",
        "lossy": lossy,
        "source": "test",
    }


def test_helper_prompt_builder_keeps_payload_narrow() -> None:
    pack = get_retrieval_pack("row_selection")
    artifact = _artifact("compact_families", "D1|ul>li|4|D|P|slot:a[href]|sig:a", True)
    payload = build_interpret_payload(
        goal_text="extract title and link",
        artifact=artifact,
        constraints=["use PROJECT"],
        retrieval_pack=pack,
    )
    assert set(payload.keys()) == {"task", "goal_text", "artifact", "constraints", "retrieval_pack", "current_query"}

    repair = build_repair_payload(
        current_query="SELECT FROM doc",
        diagnosis="grammar",
        retrieval_pack=pack,
        constraints=["CTE only"],
    )
    assert set(repair.keys()) == {"task", "current_query", "diagnosis", "constraints", "retrieval_pack"}


def test_helper_mock_model_e2e_suggest_start() -> None:
    adapters = FakeAdapters(
        artifacts={
            "compact_families": _artifact("compact_families", "D1|ul>li|4|D|P|slot:a[href]|sig:a", True),
            "families": _artifact("families", "family detail", True),
            "skeleton": _artifact("skeleton", "html\n└── body", True),
            "targeted_subtree": _artifact("targeted_subtree", "<ul><li>x</li></ul>", False),
        },
        lint_summary={
            "ok": True,
            "category": "none",
            "error_count": 0,
            "warning_count": 0,
            "headline": "",
            "details": "",
        },
        execution_summary={
            "ok": True,
            "row_count": 4,
            "null_field_count": 0,
            "blank_field_count": 0,
            "has_rows": True,
            "expected_row_count": None,
            "headline": "4 rows returned",
            "details": "rows returned from test",
            "sample_rows": [{"node_id": "1"}],
        },
    )
    model = MockModelAdapter(
        suggest_responses=[
            {
                "status": "query_ready",
                "diagnosis": "row_scope",
                "reason": "row check first",
                "chosen_family": "D1",
                "requested_artifact": "",
                "query": "SELECT self.node_id FROM doc WHERE tag = 'li' ORDER BY node_id LIMIT 10;",
                "next_action": "lint",
            }
        ]
    )
    result = suggest_query(
        input_path="fixture.html",
        goal_text="extract title and link",
        tool_adapters=adapters,
        model_adapter=model,
    )
    assert result["status"] == "done"
    assert "SELECT self.node_id" in result["query"]
    assert adapters.inspect_calls == ["compact_families"]
    assert len(adapters.lint_calls) == 1
    assert len(adapters.execute_calls) == 1
    assert [name for name, _ in model.calls] == ["interpret_and_suggest"]


def test_helper_escalates_one_artifact_level_at_a_time() -> None:
    adapters = FakeAdapters(
        artifacts={
            "compact_families": _artifact("compact_families", "D1|div>div|3|D|P|slot:span|sig:div", True),
            "families": _artifact("families", "family detail for D1", True),
            "skeleton": _artifact("skeleton", "body", True),
            "targeted_subtree": _artifact("targeted_subtree", "<div>target</div>", False),
        },
        lint_summary={
            "ok": True,
            "category": "none",
            "error_count": 0,
            "warning_count": 0,
            "headline": "",
            "details": "",
        },
        execution_summary={
            "ok": True,
            "row_count": 1,
            "null_field_count": 0,
            "blank_field_count": 0,
            "has_rows": True,
            "expected_row_count": None,
            "headline": "1 row returned",
            "details": "rows returned from test",
            "sample_rows": [{"node_id": "1"}],
        },
    )
    model = MockModelAdapter(
        suggest_responses=[
            {
                "status": "need_more_artifact",
                "diagnosis": "artifact_too_lossy",
                "reason": "need fuller family detail",
                "chosen_family": "D1",
                "requested_artifact": "families",
                "query": "",
                "next_action": "escalate",
            },
            {
                "status": "query_ready",
                "diagnosis": "row_scope",
                "reason": "families detail is enough",
                "chosen_family": "D1",
                "requested_artifact": "",
                "query": "SELECT self.node_id FROM doc WHERE tag = 'div' ORDER BY node_id LIMIT 10;",
                "next_action": "lint",
            },
        ]
    )
    result = suggest_query(
        input_path="fixture.html",
        goal_text="extract cards",
        tool_adapters=adapters,
        model_adapter=model,
    )
    assert result["status"] == "done"
    assert adapters.inspect_calls == ["compact_families", "families"]


def test_helper_no_full_html_on_common_success_path() -> None:
    adapters = FakeAdapters(
        artifacts={
            "compact_families": _artifact("compact_families", "D1|ul>li|4|D|P|slot:a[href]|sig:a", True),
            "families": _artifact("families", "family detail", True),
            "skeleton": _artifact("skeleton", "body", True),
            "targeted_subtree": _artifact("targeted_subtree", "<ul><li>x</li></ul>", False),
        },
        lint_summary={
            "ok": True,
            "category": "none",
            "error_count": 0,
            "warning_count": 0,
            "headline": "",
            "details": "",
        },
        execution_summary={
            "ok": True,
            "row_count": 2,
            "null_field_count": 0,
            "blank_field_count": 0,
            "has_rows": True,
            "expected_row_count": None,
            "headline": "2 rows returned",
            "details": "rows returned from test",
            "sample_rows": [{"node_id": "1"}],
        },
    )
    heuristic = HeuristicModelAdapter()
    result = suggest_query(
        input_path="fixture.html",
        goal_text="extract cards",
        tool_adapters=adapters,
        model_adapter=heuristic,
    )
    assert result["status"] == "done"
    assert "full_html" not in adapters.inspect_calls


def test_helper_constraint_obedience_prefers_project() -> None:
    heuristic = HeuristicModelAdapter()
    decision = heuristic.interpret_and_suggest(
        {
            "goal_text": "extract title and company",
            "artifact": _artifact("compact_families", "D1|ul>li|4|D|P|slot:a[href]|sig:a", True),
            "constraints": ["use PROJECT"],
            "retrieval_pack": get_retrieval_pack("stable_extraction"),
            "current_query": "",
        }
    )
    assert decision["status"] == "query_ready"
    assert "PROJECT(" in decision["query"]


def test_markql_public_helper_exports_exist() -> None:
    assert callable(markql.suggest_query)
    assert callable(markql.repair_query)
    assert callable(markql.explain_query)
