# MarkQL Refactor Phase 1 Decision Ledger

Date: 2026-05-28

This ledger captures the locked Phase 1 design decisions for the MarkQL C++ refactor and possible future Rust migration. It is not the normative language spec and it is not ADR-0009. The next step is to review this ledger, then draft ADR-0009 from the approved decisions.

## Scope

MarkQL remains a SQL-style query engine for HTML. The semantic center is the two-stage model:

- outer `WHERE` decides row survival
- field expressions decide supplier and value selection

The refactor keeps C++ as the active implementation path. Rust remains a deferred optional memo.

## Locked Decisions

### Direction

- Q1/Q4: Refactor C++ in place. Rust migration is deferred as an optional future memo.
- Q5: The Phase 1 spec deliverable must cover grammar, semantics, examples, diagnostics catalog, public API, and non-goals.
- Q6: Use literate Markdown specs with stable IDs.
- Q7: Enforce the existing two-tier MarkQL model rather than inventing a new one.
- Q8: The earlier broad "disjoint identifier sets" framing is superseded by fact-finding.

### SQL-Faithful Surface

- Q9: Alias is canonical. Remove `self` from the canonical surface and remove bare-tag-as-row as a canonical row-reference form.
- Q10: Use implicit-when-unambiguous SQL scoping.
- Q11: Use standard lexical scoping: innermost scope wins for bare fields.
- Q14: Replace node-row projection `SELECT self` with `SELECT alias.*`.
- Q15: Add narrow `SELECT alias.*` support before running migration.
- Q18: In nested scopes, bare fields bind to the innermost current row. Qualify only outer-scope or different-row references.

Canonical style:

```sql
SELECT n.node_id,
       PROJECT(n) AS (
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc AS n
WHERE tag = 'section'
  AND attributes.data-kind = 'flight';
```

## Migration

- Q12: Build an internal migration tool first, not a public CLI command.
- Q13: Implement it as a C++ source-span patcher using the existing parser AST.
- Q16: Avoid misleading aliases such as `FROM doc AS section`. Use short neutral aliases such as `n`, while keeping tag identity in `WHERE tag = 'section'`.
- Q17: The codemod may insert explicit tag predicates when legacy tag projections imply row selection, but it should not over-qualify fields when R1 scoping makes bare fields unambiguous.

Migration tool shape:

```text
tools/markql_migrate_sql_faithful
  --check
  --diff
  --write
```

Migration guardrails:

- rewrite only proven-safe spans
- reject overlapping edits
- report review-needed cases instead of guessing
- preserve formatting where possible
- do not make migration a public CLI contract yet

## Diagnostics

- Q19: Keep the existing diagnostic structure. Improve message quality and priority mappings.
- Q20: Promote deterministic language-contract failures to stable `MQL-SEM-*` or `MQL-LINT-*` diagnostics. Keep true IO, network, and environment failures as generic runtime diagnostics.

Diagnostic quality rubric:

- identify the stage: parse, row boundary, field boundary, sink boundary
- state the precise failure
- explain why it matters in the two-stage model
- show what to write instead
- keep code, span, JSON shape, and wording deterministic

Priority diagnostic work:

- removed `self`: use the current alias or `alias.*`
- legacy tag-as-row: move tag identity into `WHERE tag = ...`
- alias-as-value: update help from `SELECT self` to `SELECT alias.*`
- top-level vs `PROJECT(...)` `TEXT(...)` confusion
- runtime throws such as mixed SELECT shapes and text helper guardrails

## Spec

- Q21: Create a modular authoritative spec under `specs/markql/`, separate from `specs/junks/`.

Proposed layout:

```text
specs/markql/
  README.md
  01-grammar.md
  02-row-scope-and-selection.md
  03-field-extraction.md
  04-functions.md
  05-diagnostics.md
  06-public-api.md
  07-non-goals.md
  examples/
    sql-faithful.md
```

## C++ Refactor

- Q22: Use a boundary-first refactor. Stabilize parser, AST, validator, diagnostics, runtime, CLI, and public API boundaries before doing broad file-size cleanup.
- Q23: Use libxml2 as the only supported DOM backend. Remove the naive parser from normal builds.

Boundary-first order:

- add SQL-faithful syntax and validation needed by migration
- move deterministic validation failures into diagnostics
- make parser, validator, and runtime ownership explicit
- consolidate or split files only where it clarifies those boundaries

## Public API

- Q25: Add a stable rectangular result view layered over `QueryResult`.
- Do not make NumPy a core dependency.
- Python should provide ergonomic adapters such as `to_records()`, `to_columns()`, and optional `to_pandas()` when pandas is installed.

## Rust

- Q24: Rust remains a short deferred optional memo after Phase 1.
- No Rust implementation issues are created now.

Memo contents later:

- preconditions for reconsidering Rust
- triggers that would make Rust worth revisiting
- explicit non-goal: no rewrite before those triggers appear

## Phase 2 Sequence

- Q26: Use spec-first sequencing with a thin executable tracer bullet.

Recommended order:

1. Create `specs/markql/` skeleton with stable IDs.
2. Add narrow `SELECT alias.*` support and diagnostics.
3. Add the internal codemod in `tools/`.
4. Update docs, examples, and tests through the codemod.
5. Improve priority diagnostics.
6. Remove the naive DOM backend.
7. Add rectangular API adapters.
8. Do boundary-first C++ cleanup.
9. Draft ADR-0009 and the short Rust deferred memo.

## Verification

- Q27: Use staged verification per slice, with full regression at phase boundaries.

Slice gates:

- grammar/parser change: parser tests and targeted lint examples
- runtime semantics change: relevant CTest subset
- diagnostics change: diagnostic tests and golden snippets
- docs/examples change: `./docs/verify_examples.sh`
- Python API change: `./test_python.sh`

Phase close-out gate:

```bash
./build.sh
ctest --test-dir build --output-on-failure
./docs/verify_examples.sh
./test_python.sh
```

Run Python tests only when Python bindings or adapters changed.

## Not Doing

- no public migration CLI in Phase 2
- no Rust rewrite
- no ADR-0009 until this ledger is approved
- no `CONTEXT.md` updates for language-design terms
- no extension of `specs/junks/`
- no broad formatter or whole-query pretty-printer as the first migration tool
- no naive DOM backend after the backend cleanup slice

## ADR-0009 Candidate Scope

ADR-0009 should record:

- spec-first C++ refactor
- SQL-faithful alias-based surface
- internal codemod before public migration command
- diagnostics quality plan
- libxml2-only DOM backend
- Rust deferred as optional future work

ADR-0009 should not include the full language spec or implementation checklist. Those belong in `specs/markql/` and Phase 2 issues.
