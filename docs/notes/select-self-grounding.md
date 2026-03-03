# Maintainer Note: `SELECT self` Grounding (2026-03-03)

This note captures the pre-change spec/runtime baseline used for the `SELECT self` canonicalization work.

## Current spec truths (before this change)

- `self` is documented as "the current row node" in `WHERE` and extraction function args (`TEXT(self)`, `ATTR(self, ...)`, `INNER_HTML(self, ...)`).
- Projection docs emphasize:
  - tag-row projection (`SELECT div FROM doc`)
  - field projection (`SELECT a.href`)
  - current-row field projection (`SELECT self.node_id`, `SELECT self.tag`).
- Lint diagnostics are documented and implemented with stable fields:
  - `severity`, `code`, `message`, `help`, `doc_ref`, spans, snippets.
- Deprecation precedent exists and is backward-compatible:
  - `FRAGMENTS(...)` remains supported but emits warnings to migrate to `PARSE(...)`.
- Existing docs/case-study SQL include `CROSS JOIN LATERAL` subqueries that use:
  - `SELECT <from_alias>`
  - e.g. `SELECT node_badge FROM doc AS node_badge`.

## Runtime behavior baseline (important)

- In relation-runtime shapes (CTE/JOIN/LATERAL), `SELECT <from_alias>` can act as row-node return sugar.
- In plain node-stream execution (non relation-runtime), `SELECT <from_alias>` is not universally equivalent to "current row node";
  it can behave like tag selection and may return no rows when alias is not a real tag name.

## Design constraint derived from baseline

- Canonicalize to `SELECT self` for explicit "current row node" projection.
- Keep legacy `SELECT <from_alias>` behavior unchanged for compatibility.
- Add lint migration guidance instead of changing old query semantics.
