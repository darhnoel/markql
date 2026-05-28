---
name: markql-query-drafter
description: Draft or extend MarkQL extraction queries from a concrete goal plus a fixture, DOM clue, or html_inspector artifact. Use for row selection, row gating, one-field extraction, FLATTEN-to-PROJECT progression, and stable case-study query updates. Do not use for parser/runtime code changes or for stage-based debugging of an already failing query.
---

# MarkQL Query Drafter

Use this skill when the task is to write or extend a MarkQL query, usually in a docs query file, case study, exercise, or reproduction.

## Use When
- The user wants a new MarkQL query or a modification to an existing query.
- The task starts from a fixture, page snapshot, family artifact, or case-study goal.
- You need to choose rows, then fields, then a stable output shape.
- You are using `html_inspector` artifacts to draft the next query.

## Do Not Use When
- The main task is changing parser, validator, executor, CLI, or diagnostics code.
- The main task is troubleshooting an existing query failure or null-heavy regression.
- The repo lacks enough input evidence and the real next step is diagnosis, not drafting.

Negative examples:
- “Add a new `ORDER BY` grammar form.” Use `markql-language-change`.
- “Explain why this query now returns zero rows.” Use `markql-troubleshooter`.

## Expected Inputs
- A concrete extraction goal.
- One of:
  - a local fixture or snapshot path
  - an existing MarkQL query file
  - a bounded `html_inspector` artifact (`--families-compact`, `--skeleton`, targeted subtree)
- Any user constraints such as `CTE only`, `no PROJECT`, or `no FLATTEN`.

## Required Workflow
1. Check the current language contract before relying on syntax.
   - Use `specs/markql/` for SQL-faithful canonical style.
   - Use `docs/book/appendix-grammar.md` and `docs/markql-cli-guide.md` for currently accepted syntax.
2. Protect the context window before inspecting input.
   - Do not read a whole HTML fixture, page snapshot, `--families` dump, or raw artifact into chat.
   - Start with size/count probes such as `wc`, `rg --count`, `COUNT(tag)`, and `SUMMARIZE(*)`.
   - Avoid broad `rg ... file | head` on minified HTML; one matching line can be huge. Prefer precise patterns, `rg --max-count`, MarkQL counts, or redirect large output to `/tmp` and inspect small slices.
   - Use `html_inspector --families-compact` first.
   - Do not run `html_inspector --families` by default. It is too verbose for normal agent use; require explicit user approval or an already-provided filtered artifact.
   - Read raw HTML only as a bounded slice around a proven anchor line, not as the discovery mechanism.
3. Keep the two-stage model explicit.
   - Outer `WHERE` decides row survival.
   - Field expressions decide suppliers for kept rows.
4. Draft in SQL-faithful style unless the user is deliberately maintaining legacy examples.
   - Use aliases as row references.
   - Use bare fields when they bind to the current row unambiguously.
   - Qualify outer-scope or different-row references.
   - Use `SELECT alias.*` for current-node row projection.
   - Treat `SELECT self` and bare tag-as-row projection as legacy forms.
   - Keep tag identity in predicates: `FROM doc AS n WHERE tag = 'section'`.
   - Avoid misleading aliases such as `FROM doc AS section`.
   - Current `PROJECT` syntax is `PROJECT(<tag>)`, not `PROJECT(<alias>)`. Do not suggest `PROJECT(n)` for `FROM doc AS n` unless alias-aware `PROJECT` has been implemented and verified.
5. Follow the first-query loop.
   - inspect rows
   - gate rows
   - extract one value
   - scale up
6. If using `html_inspector`, use the cheapest artifact that answers the next question.
   - Start at `--families-compact`.
   - If compact mode is insufficient, prefer MarkQL probes, targeted skeleton slices, or a bounded raw slice around a proven anchor.
   - Do not escalate to full `--families` unless the user explicitly asks for it.
7. Stay narrow.
   - one family at a time
   - one query at a time
8. Choose shape deliberately.
   - Use `FLATTEN` for fast discovery on unknown structure.
   - Move to `PROJECT` for stable named columns.
   - For row-shaped extraction, gate rows with `WHERE tag = '<row-tag>'`, then use `PROJECT(<row-tag>) AS (...)`.
   - Prefer a row-anchor `WITH` once the query stops being trivial.
9. Respect CTE purity.
   - Helper CTEs may carry row ids and scalar helpers.
   - `PROJECT(...)` or `FLATTEN(...)` inside a CTE must be a pure shaped relation.
   - Do not mix row-id helper columns with shaped extraction in the same CTE.
10. Lint first, then execute if needed.
11. Do not invent grammar or capabilities not already documented in this build.

## Verification Checklist
- The row set is proven before rich field extraction.
- Large files/artifacts were inspected through bounded probes, not loaded wholesale.
- Bare names are checked against lexical scope: innermost row wins.
- Tag constraints are in `WHERE`, not hidden in aliases.
- If fields are null, interpret them as supplier problems unless row scope is still unproven.
- The query uses documented grammar from `docs/book/appendix-grammar.md` and `docs/markql-cli-guide.md`.
- If `FLATTEN` was used for discovery and the output is meant to be stable, convert to `PROJECT`.
- If output shape matters, choose the sink only after schema is stable.
- Run `./build/markql --lint "<query>"` before relying on execution output.

## Output Contract
- Return exactly one next query shape or one concrete query-file edit.
- Keep the explanation short and tied to row scope, supplier scope, or artifact choice.
- If blocked by insufficient evidence, request a bounded next artifact instead of guessing a larger query.

## Repo References
- `docs/book/ch02-mental-model.md`
- `docs/book/ch03-first-query-loop.md`
- `docs/book/ch08-flatten.md`
- `docs/book/ch09-project.md`
- `specs/markql/README.md`
- `specs/markql/01-grammar.md`
- `specs/markql/02-row-scope-and-selection.md`
- `specs/markql/examples/sql-faithful.md`
- `docs/book/appendix-grammar.md`
- `docs/markql-cli-guide.md`
- `tools/html_inspector/docs/ai_inspection_playbook.md`
- `tools/html_inspector/docs/ai_markql_musts.md`
- `docs/case-studies/field-hunting-playbook.md`
