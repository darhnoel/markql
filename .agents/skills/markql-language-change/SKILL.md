---
name: markql-language-change
description: Change MarkQL syntax, semantics, validation, diagnostics, execution, or CLI language behavior while preserving documented compatibility and updating tests/docs together. Use for parser, AST, validator, executor, sink, lint, and user-visible language-contract work. Do not use for query authoring or isolated query troubleshooting.
---

# MarkQL Language Change

Use this skill when the repository change affects MarkQL behavior rather than a single query file.

## Use When
- Adding or changing grammar, AST shapes, validation rules, diagnostics, query execution, or sink behavior.
- Changing documented language contracts such as `PROJECT`, `FLATTEN`, `EXISTS`, sources, artifacts, or lint output.
- Updating compatibility behavior around deprecated or legacy query forms.

## Do Not Use When
- The task is only to draft or repair a query.
- The task is only to diagnose one failing query instance.
- The task is formal verification planning; no repo-grounded markdown plan exists for that workflow yet.

Negative examples:
- “Update this case-study SQL file.” Use `markql-query-drafter`.
- “Why does this query produce NULL for one field?” Use `markql-troubleshooter`.

## Expected Inputs
- The target behavior change.
- Relevant existing behavior in code, docs, or tests.
- Any compatibility constraint or migration note already documented in-repo.

## Required Workflow
1. Start from the documented contract, not a guessed one.
   - Check `specs/markql/` first for normative language contracts.
   - Check ADR-0009 and the Phase 1 decision ledger before changing SQL-faithful behavior.
   - Check the book chapter or appendix that owns the behavior.
   - Check `docs/markql-cli-guide.md` for CLI-facing semantics.
2. Preserve core MarkQL semantics.
   - Outer `WHERE` controls row survival.
   - Field expressions control supplier/value selection.
3. Preserve the SQL-faithful direction unless the user explicitly reopens the decision.
   - Aliases are canonical row references.
   - Bare fields use standard lexical scoping; the innermost row scope wins.
   - `SELECT alias.*` is the canonical current-node row projection.
   - `SELECT self` and bare tag-as-row forms are legacy compatibility inputs, not canonical examples.
   - Tag identity belongs in predicates such as `WHERE tag = 'section'`, not in aliases like `FROM doc AS section`.
   - Current `PROJECT` syntax is still `PROJECT(<tag>)`. Treat `PROJECT(<alias>)` as a proposed language change, not current behavior, until parser, validator, executor, tests, and docs verify it.
4. Respect implementation boundaries.
   - Parser: syntax only.
   - Validator: semantic rules and compatibility checks.
   - Executor/runtime: evaluation behavior.
   - CLI: public API and UX only.
5. Follow the implementation checklist when the change is real language work.
   - tokenizer/grammar
   - AST
   - validator/type rules
   - evaluator semantics
   - tests
   - docs
6. Preserve backward compatibility unless the repo docs already justify a break.
7. Keep diagnostics deterministic and actionable.
   - Stage the diagnostic: parse, row boundary, field boundary, sink boundary.
   - For deterministic language-contract failures, prefer stable `MQL-SEM-*` or `MQL-LINT-*` coverage.
   - Keep true IO, network, and environment failures as generic runtime diagnostics.
8. If documented command examples changed, update the docs and keep them runnable under `docs/verify_examples.sh`.
9. For legacy-to-SQL-faithful migration, use or extend the internal codemod under `tools/markql_migrate_sql_faithful`; do not make it a public CLI contract without a separate decision.

## Verification Checklist
- The changed behavior is reflected in the owning doc, appendix, or CLI guide.
- Normative behavior is reflected in `specs/markql/` when the contract changes.
- Parser/validator/executor boundaries remain intact.
- Compatibility aliases or legacy behavior remain working unless explicitly changed with docs.
- The smallest relevant test set is run:
  - C++/CTest for engine behavior
  - docs verification for documented commands
  - Python tests if bindings changed
  - CLI integration tests if command-line behavior changed
- Diagnostics stay deterministic in wording and stage meaning.

## Output Contract
- Produce the code changes plus any required doc/test updates.
- Summarize the contract change in terms of syntax, semantics, compatibility, and verification.
- Call out any still-experimental area, especially artifacts.

## Repo References
- `docs/implementation-guide.md`
- `specs/markql/README.md`
- `specs/markql/01-grammar.md`
- `specs/markql/02-row-scope-and-selection.md`
- `specs/markql/05-diagnostics.md`
- `docs/adr/0009-spec-first-markql-cpp-refactor.md`
- `docs/notes/markql-refactor-phase-1-decision-ledger.md`
- `docs/book/ch02-mental-model.md`
- `docs/book/appendix-grammar.md`
- `docs/book/ch12-troubleshooting.md`
- `docs/markql-cli-guide.md`
- `docs/book/ch04-sources-and-loading.md`
- `docs/book/SUMMARY.md`
- `tests/README.md`
- `tests/MIGRATION.md`
- `docs/verify_examples.sh`
