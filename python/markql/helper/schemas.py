from __future__ import annotations

from typing import Any, Literal, NotRequired, Protocol, TypedDict


Mode = Literal["start", "repair", "explain"]
ArtifactKind = Literal["", "compact_families", "families", "skeleton", "targeted_subtree", "full_html"]
RetrievalTopic = Literal[
    "row_selection",
    "exploration",
    "stabilization",
    "stable_extraction",
    "repair",
    "grammar",
    "null_and_scope",
]
Diagnosis = Literal[
    "none",
    "row_scope",
    "field_scope",
    "grammar",
    "semantic",
    "artifact_too_lossy",
    "mixed",
    "unknown",
]
ModelTask = Literal["none", "interpret_and_suggest", "repair_from_summary"]


class HelperRequest(TypedDict):
    mode: Mode
    input_path: str
    goal_text: str
    query: str
    constraints: list[str]
    expected_fields: list[str]


class ArtifactSummary(TypedDict):
    kind: ArtifactKind
    content: str
    selector_or_scope: str
    family_hint: str
    lossy: bool
    source: str


class RetrievalPack(TypedDict):
    topic: RetrievalTopic
    summary: str
    facts: list[str]
    examples: list[str]
    doc_refs: list[str]


class LintSummary(TypedDict):
    ok: bool
    category: Diagnosis
    error_count: int
    warning_count: int
    headline: str
    details: str


class ExecutionSummary(TypedDict):
    ok: bool
    row_count: int
    null_field_count: int
    blank_field_count: int
    has_rows: bool
    expected_row_count: int | None
    headline: str
    details: str
    sample_rows: list[dict[str, str]]


class ResultAnalysis(TypedDict):
    category: str
    diagnosis: Diagnosis
    reason: str
    should_repair: bool
    should_escalate: bool
    should_execute: bool
    done: bool


class ModelDecision(TypedDict):
    status: Literal["none", "query_ready", "need_more_artifact", "blocked"]
    diagnosis: Diagnosis
    reason: str
    chosen_family: str
    requested_artifact: ArtifactKind
    query: str
    next_action: str


class FinalSuggestion(TypedDict):
    status: Literal["done", "blocked"]
    mode: Mode
    query: str
    diagnosis: Diagnosis
    reason: str
    artifact_used: ArtifactKind
    retrieval_topic: RetrievalTopic
    lint_summary: LintSummary
    execution_summary: ExecutionSummary


class ControllerStep(TypedDict):
    state: str
    action: str
    requested_artifact: ArtifactKind
    requested_pack: RetrievalTopic
    model_task: ModelTask
    diagnosis: Diagnosis
    reason: str
    query: str
    final_suggestion: FinalSuggestion


class ControllerSnapshot(TypedDict):
    request: HelperRequest
    state: str
    current_query: str
    repair_loops: int
    artifact: NotRequired[ArtifactSummary]
    retrieval_pack: NotRequired[RetrievalPack]
    lint_summary: NotRequired[LintSummary]
    execution_summary: NotRequired[ExecutionSummary]
    result_analysis: NotRequired[ResultAnalysis]
    model_decision: NotRequired[ModelDecision]


class ToolAdapters(Protocol):
    def inspect_compact_families(self, input_path: str) -> ArtifactSummary: ...

    def inspect_families(self, input_path: str) -> ArtifactSummary: ...

    def inspect_skeleton(self, input_path: str) -> ArtifactSummary: ...

    def inspect_targeted_subtree(self, input_path: str, selector_or_scope: str) -> ArtifactSummary: ...

    def retrieve_markql_pack(self, topic: RetrievalTopic) -> RetrievalPack: ...

    def lint_markql(self, query: str, input_path: str | None = None) -> LintSummary: ...

    def run_markql(
        self,
        query: str,
        input_path: str,
        expected_fields: list[str] | None = None,
    ) -> ExecutionSummary: ...


class ModelAdapter(Protocol):
    def interpret_and_suggest(self, payload: dict[str, Any]) -> ModelDecision: ...

    def repair_from_summary(self, payload: dict[str, Any]) -> ModelDecision: ...
