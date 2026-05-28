---
name: markql-troubleshooter
description: Diagnose an existing MarkQL query or user-visible regression by classifying the failure stage first: parse, row survival, field extraction, or sink/output. Use for errors, empty results, wrong row counts, unstable NULLs, capability confusion, and minimal reproductions. Do not use for drafting a fresh extraction from scratch or for parser/runtime feature implementation.
---

# MarkQL Troubleshooter

Use this skill when a query already exists and the task is to explain or fix why it fails.

## Use When
- A query fails to parse or validate.
- The query runs but returns zero rows, wrong rows, wrong ordering, or unstable nulls.
- The query shape and sink look mismatched.
- You need a smallest reproduction and a stage-aware fix.

## Do Not Use When
- The user is asking for a new extraction query from a fixture or artifact.
- The main task is changing MarkQL language/runtime implementation.

Negative examples:
- “Write a query for this new case study.” Use `markql-query-drafter`.
- “Implement support for `ORDER BY attributes.foo`.” Use `markql-language-change`.

## Expected Inputs
- The failing query text or query file.
- A local input fixture, prepared artifact, or the exact failing command.
- The observed symptom:
  - parse/validation error
  - empty/wrong rows
  - null fields
  - sink/export failure

## Required Workflow
1. Protect the context window before inspecting input.
   - Do not read a whole HTML fixture, page snapshot, artifact dump, or large query output into chat.
   - Start with bounded probes: exact failing command, lint output, `COUNT(tag)`, `SUMMARIZE(*)`, `wc`, and precise `rg --max-count` patterns.
   - Avoid broad grep over minified HTML because one matching line can be huge.
   - Do not run `html_inspector --families` by default. It is too verbose for normal agent use; require explicit user approval or an already-provided filtered artifact.
   - Redirect large artifacts to `/tmp` and inspect only the relevant slice.
2. Reproduce with the smallest practical fixture and smallest query.
3. Classify the failure stage before editing.
   - parse boundary
   - row survival boundary
   - field extraction boundary
   - sink/output boundary
4. Lint first.
5. Verify row scope before field scope.
6. Apply current SQL-faithful scope rules when explaining failures.
   - Bare fields bind by lexical scope; the innermost row wins.
   - Qualify outer row references with aliases.
   - `SELECT alias.*` is canonical for current-node row projection.
   - `SELECT self` and bare tag-as-row forms are legacy compatibility inputs.
   - If an alias looks like a tag name, do not infer a tag constraint from it; look for `WHERE tag = ...`.
   - `PROJECT(<tag>)` is current syntax. If a query uses `PROJECT(<alias>)`, classify it as unsupported syntax unless alias-aware `PROJECT` has landed in this build.
7. If capability is unclear, confirm supported features with:
   - `SHOW FUNCTIONS;`
   - `SHOW AXES;`
   - `SHOW OPERATORS;`
8. Keep one deliberate failure query or exact failing command when it helps preserve the regression boundary.
9. Apply the smallest stage-specific fix, then rerun the same reproduction.
10. If the query was drafted from `html_inspector` artifacts and evidence is too lossy, request a bounded next artifact; do not escalate to full `--families` unless the user explicitly asks for it.

## Verification Checklist
- There is an exact reproduction command or lint invocation.
- Large files/artifacts were inspected through bounded probes, not loaded wholesale.
- The diagnosed stage is explicit.
- Row count is validated before field-level fixes.
- Scope explanations distinguish current-row, nested-row, and outer-row references.
- Nulls are interpreted as signal:
  - all rows wrong -> row gate issue
  - correct rows, one field null -> supplier issue
  - some rows good, some null -> mixed family or unstable supplier
- Sink fixes respect documented sink constraints.
- The corrected query still uses documented grammar only.

## Output Contract
- State the failure stage.
- Provide one corrected query, one targeted code/doc edit, or one next reproduction command.
- Include the minimal verification command used to confirm the fix.

## Repo References
- `docs/book/ch02-mental-model.md`
- `docs/book/ch03-first-query-loop.md`
- `docs/book/ch12-troubleshooting.md`
- `docs/book/appendix-grammar.md`
- `specs/markql/01-grammar.md`
- `specs/markql/02-row-scope-and-selection.md`
- `specs/markql/05-diagnostics.md`
- `docs/markql-cli-guide.md`
- `tools/html_inspector/docs/ai_inspection_playbook.md`
- `tools/html_inspector/docs/ai_markql_musts.md`
