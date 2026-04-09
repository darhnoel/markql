# MarkQL Helper Runtime Contracts

## Overview

This document defines the machine-facing contracts used by the helper controller, adapters, prompt builder, and model adapter.

All contracts are compact and deterministic.

## Enumerations

### Mode
- `start`
- `repair`
- `explain`

### Artifact Kind
- `compact_families`
- `families`
- `skeleton`
- `targeted_subtree`
- `full_html`

### Controller State
- `START`
- `INSPECT_COMPACT`
- `CHOOSE_PATH`
- `RETRIEVE_PACK`
- `MODEL_SUGGEST`
- `LINT_QUERY`
- `EXECUTE_QUERY`
- `ANALYZE_RESULT`
- `MODEL_REPAIR`
- `ESCALATE_ARTIFACT`
- `DONE`
- `BLOCKED`

### Diagnosis
- `none`
- `row_scope`
- `field_scope`
- `grammar`
- `semantic`
- `artifact_too_lossy`
- `mixed`
- `unknown`

### Analysis Category
- `grammar_failure`
- `semantic_failure`
- `empty_output`
- `wrong_row_count`
- `right_rows_null_fields`
- `mixed_instability`
- `likely_success`
- `artifact_too_lossy`

### Pack Topic
- `row_selection`
- `exploration`
- `stabilization`
- `stable_extraction`
- `repair`
- `grammar`
- `null_and_scope`

## HelperRequest

```json
{
  "mode": "start|repair|explain",
  "input_path": "page.html",
  "goal_text": "extract title, company, salary, link",
  "query": "",
  "constraints": ["use PROJECT", "one query only"],
  "expected_fields": ["title", "company", "salary", "link"]
}
```

Notes:
- `query` may be empty for `start`
- `constraints` are literal user constraints
- `expected_fields` is optional but improves result summaries

## Intent

```json
{
  "mode": "start|repair|explain",
  "fields": ["title", "company"],
  "constraints": ["no PROJECT"],
  "goal_text": "extract title and company"
}
```

## ArtifactSummary

```json
{
  "kind": "compact_families|families|skeleton|targeted_subtree|full_html",
  "content": "artifact text",
  "selector_or_scope": "",
  "family_hint": "",
  "lossy": true,
  "source": "html_inspector|slice|raw"
}
```

## RetrievalPack

```json
{
  "topic": "row_selection",
  "summary": "outer WHERE controls row survival",
  "facts": [
    "Use EXISTS(...) in outer WHERE for row inclusion",
    "Keep row scope separate from field scope"
  ],
  "examples": [
    "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(child WHERE tag = 'h3')"
  ],
  "doc_refs": [
    "docs/book/ch02-mental-model.md",
    "docs/book/ch03-first-query-loop.md"
  ]
}
```

## LintSummary

```json
{
  "ok": false,
  "category": "grammar|semantic|none",
  "error_count": 1,
  "warning_count": 0,
  "headline": "Missing projection after SELECT",
  "details": "Expected projection after SELECT"
}
```

## ExecutionSummary

```json
{
  "ok": true,
  "row_count": 12,
  "null_field_count": 0,
  "blank_field_count": 0,
  "has_rows": true,
  "headline": "12 rows returned",
  "details": "rows look non-empty",
  "sample_rows": [
    {"title": "A", "company": "B"}
  ]
}
```

## ResultAnalysis

```json
{
  "category": "wrong_row_count",
  "diagnosis": "row_scope",
  "reason": "rows are empty for a start-mode row-check query",
  "should_repair": true,
  "should_escalate": false,
  "should_execute": false,
  "done": false
}
```

## ModelDecision

```json
{
  "status": "query_ready|need_more_artifact|blocked",
  "diagnosis": "row_scope|field_scope|grammar|semantic|artifact_too_lossy|mixed|unknown",
  "reason": "short string",
  "chosen_family": "D6",
  "requested_artifact": "compact_families|families|skeleton|targeted_subtree|full_html|",
  "query": "SELECT ...",
  "next_action": "lint|execute|escalate|done"
}
```

Rules:
- exactly one `query`
- no multiple candidate queries
- `requested_artifact` is empty unless `status = need_more_artifact`

## FinalSuggestion

```json
{
  "status": "done|blocked",
  "mode": "start|repair|explain",
  "query": "SELECT ...",
  "diagnosis": "row_scope",
  "reason": "repair row scope before field extraction",
  "artifact_used": "compact_families",
  "retrieval_topic": "row_selection",
  "lint_summary": {
    "ok": true,
    "category": "none",
    "error_count": 0,
    "warning_count": 0,
    "headline": "",
    "details": ""
  },
  "execution_summary": {
    "ok": true,
    "row_count": 5,
    "null_field_count": 0,
    "blank_field_count": 0,
    "has_rows": true,
    "headline": "",
    "details": "",
    "sample_rows": []
  }
}
```

## Model Task Contracts

### interpret_and_suggest

Input:
- `intent`
- `artifact`
- `constraints`
- `retrieval_pack`
- `current_query`
- `lint_summary`
- `execution_summary`

Output:
- one `ModelDecision`

### repair_from_summary

Input:
- `current_query`
- `diagnosis`
- `lint_summary`
- `execution_summary`
- `constraints`
- `retrieval_pack`

Output:
- one `ModelDecision`

## Prompt Minimization Rules

The prompt builder must include only:
- user goal
- one artifact
- one retrieval pack
- one current query if any
- one short lint or execution summary if any
- explicit user constraints

The prompt builder must not include:
- full docs
- multiple artifacts
- multiple families
- multiple candidate queries
- direct extracted answers
