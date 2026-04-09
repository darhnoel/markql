# MarkQL Helper Evaluation

## Goal

Evaluation proves that the helper behaves as a bounded MarkQL-next-step system rather than an unconstrained agent wrapper.

## Test Matrix

### Policy and controller
- common path starts from `compact_families`
- controller uses one query at a time
- controller allows at most two repair loops
- controller escalates only one artifact level at a time
- controller does not jump to full HTML on the common path

### Result analysis
- grammar failures map to grammar repair
- semantic or lint failures map to semantic repair
- empty output maps to row-scope repair unless evidence points elsewhere
- wrong row counts map to row-scope repair
- correct rows with null fields map to field-scope repair
- mixed signals map to mixed instability
- lossy artifacts map to one-level escalation
- likely-success results stop the loop

### Constraint obedience
- `no PROJECT` forbids `PROJECT`
- `no FLATTEN` forbids `FLATTEN`
- `use PROJECT` prefers `PROJECT`
- `use EXISTS` preserves `EXISTS`
- `CTE only` forbids non-CTE shapes

### Model boundaries
- only `interpret_and_suggest` and `repair_from_summary` are used
- tests run with a mock model adapter only
- prompts stay narrow and artifact-scoped

### End-to-end flows
- start flow with natural-language goal
- repair flow from parse error
- repair flow from wrong rows
- repair flow from null fields
- artifact escalation from compact to families
- blocked outcome when neither query nor one-step escalation is available

### Regression safety
- existing MarkQL execution and lint behavior remain unchanged
- helper tests do not mutate core query semantics

## Fixtures

Helper tests use deterministic local fixtures only.

Preferred inputs:
- small local HTML pages
- compact artifact text fixtures
- mock model decisions

No helper test requires:
- live network
- live provider credentials
- full-HTML-first prompting

## Success Signals

The helper implementation is considered acceptable when:
- deterministic policy tests pass
- mock-model end-to-end tests pass
- common path avoids premature full HTML
- existing MarkQL behavior tests continue to pass

## Optional Evaluation Harness

`scripts/run_helper_eval.py` runs a small local evaluation matrix and prints:
- scenario
- chosen artifact level
- chosen retrieval pack
- query produced
- whether the result stopped, repaired, or escalated

This harness is informative and deterministic. It does not require a live model.
