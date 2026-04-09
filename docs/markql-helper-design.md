# MarkQL Helper Design

## Purpose

MarkQL Helper is a bounded suggestion system for users who are stuck and do not know what MarkQL query to write next.

The helper:
- returns one next MarkQL step
- returns MarkQL queries, not extracted answers
- keeps the common path local and deterministic
- uses model assistance only for bounded intent parsing and bounded query suggestion/repair

## Scope

Covered user situations:
- natural-language extraction goal with no query yet
- existing query with parse or lint error
- query runs but returns empty output or wrong rows
- rows look correct but fields are `NULL`
- current artifact is too lossy to continue safely

Out of scope:
- direct extraction answers without MarkQL
- blind retry loops
- raw-HTML-first prompting
- multiple candidate queries per step

## Architecture

The implementation is split into:

1. C++ helper core
   - contracts
   - retrieval packs
   - deterministic controller/policy
   - deterministic result analyzer
   - lightweight JSON conversion helpers for structured contracts

2. Python helper surface
   - orchestrator loop
   - local tool adapters
   - prompt builder
   - model adapter interface
   - mock model adapter
   - CLI

This split keeps the MarkQL-specific deterministic logic close to the core while keeping provider-specific and subprocess-based integration out of the engine.

## Why This Placement

The repository already has:
- `markql_core` as the C++ semantic core
- pybind bindings in `python/markql/_core.cpp`
- a Python package surface in `python/markql`

The helper extends those existing seams instead of creating a parallel package root. The Python surface lives under `python/markql/helper/`.

## Deterministic Backbone

The local backbone owns:
- lint
- execution
- deterministic result classification
- retrieval-pack selection
- artifact escalation policy
- step-by-step controller decisions

The backbone does not:
- call a live model
- parse natural language heuristically beyond explicit user constraints
- inspect full HTML first

## Controller Model

The controller is a bounded state machine with these states:
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

The controller is implemented as a step engine in C++.

Python orchestration drives the loop:
- executes local adapters when the controller asks for local work
- invokes the model adapter only when the controller asks for model work
- feeds compact summaries back into the controller

## Hard Policy Rules

These rules are enforced in the controller and tests:
- one family per attempt
- one query per iteration
- maximum two repair loops per attempt
- maximum one artifact escalation level at a time
- no full HTML unless required
- stop when one good next query is ready
- row-count failures are repaired before field logic
- `NULL` field issues are treated as supplier failures when rows are otherwise correct
- user constraints are obeyed literally

## Artifact Policy

Escalation order:

1. `compact_families`
2. `families`
3. `skeleton`
4. `targeted_subtree`
5. `full_html`

The helper starts at `compact_families` on the common path.

Escalation happens only when the analyzer or model decision marks the current artifact as too lossy for the next useful step.

## Retrieval Packs

The helper ships small retrieval packs assembled from existing docs and verified examples:
- `row_selection`
- `exploration`
- `stabilization`
- `stable_extraction`
- `repair`
- `grammar`
- `null_and_scope`

Each pack contains only:
- a short topic label
- a few fact snippets
- one or two doc-grounded examples
- owning doc references

The helper never sends the full manual to the model.

## Model Usage

Model usage is optional and limited to two tasks:

1. `interpret_and_suggest`
2. `repair_from_summary`

Model inputs are narrow:
- user goal
- one artifact
- one retrieval pack
- current query if any
- short lint or execution summary if any
- explicit user constraints

Model outputs are structured and limited to:
- one next query
- or one artifact escalation request

The model is not used for:
- executing queries
- validating correctness
- classifying row-scope vs field-scope failures
- deciding whether full HTML is needed without deterministic evidence

## Local Tool Adapters

Python adapters wrap local functionality behind stable interfaces:
- `inspect_compact_families`
- `inspect_families`
- `inspect_skeleton`
- `inspect_targeted_subtree`
- `retrieve_markql_pack`
- `lint_markql`
- `run_markql`
- `summarize_result`

Only the adapter layer is allowed to invoke subprocess-backed tools such as `html_inspector`.

## Explanation Mode

`explain` uses the same controller and analyzer but stops after producing:
- the diagnosed failure category
- the next useful query or escalation request
- short reasoning anchored to row scope vs field scope

It still returns MarkQL-oriented guidance rather than raw extraction answers.

## File Layout

New C++ core files:
- `core/src/helper/helper_types.h`
- `core/src/helper/helper_json.h`
- `core/src/helper/helper_json.cpp`
- `core/src/helper/helper_policy.h`
- `core/src/helper/helper_policy.cpp`
- `core/src/helper/helper_result_analysis.h`
- `core/src/helper/helper_result_analysis.cpp`
- `core/src/helper/helper_controller.h`
- `core/src/helper/helper_controller.cpp`

New Python files:
- `python/markql/helper/__init__.py`
- `python/markql/helper/schemas.py`
- `python/markql/helper/retrieval_packs.py`
- `python/markql/helper/prompt_builder.py`
- `python/markql/helper/model_adapter.py`
- `python/markql/helper/tool_adapters.py`
- `python/markql/helper/orchestrator.py`
- `python/markql/helper/cli.py`

Docs:
- `docs/markql-helper-design.md`
- `docs/markql-helper-runtime-contracts.md`
- `docs/markql-helper-evaluation.md`

## Verification Strategy

Verification is split by boundary:
- C++ tests for controller decisions, retrieval-pack selection, and result analysis
- Python tests for prompt building, adapter orchestration, constraint obedience, escalation policy, and mock-model end-to-end behavior
- existing query behavior tests remain unchanged

## Compatibility

The helper is additive. It does not change MarkQL grammar or executor behavior.

User-facing surface changes:
- new Python helper APIs
- new `markql-helper` CLI entrypoint
- new documented helper contracts

Version metadata must therefore be updated.
