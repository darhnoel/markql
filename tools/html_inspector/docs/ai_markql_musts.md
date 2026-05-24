# AI MarkQL Musts

This document is a strict agent contract for suggesting MarkQL from `html_inspector` artifacts.

It is based on verified local runs, not only on intended syntax.

Use it when the goal is:
- low-token query drafting
- high hit rate
- minimal retry loops

## Hard Rules

### 1. Treat compact family output as a hypothesis

`--families-compact` is a lossy summary.

Use it to choose one candidate family.
Do not treat it as full DOM truth.

If the next query fails or returns unexpected nulls, escalate one artifact level.

### 2. Follow the checkpoint order

Do not jump to a rich extraction query first.

Required progression:

1. row check
2. row gate
3. row-anchor `WITH`
4. one-field extraction
5. full stable extraction

### 3. One family at a time

Pick one family id for the next query.
Do not mix families in one suggestion.

### 4. One query at a time

Return one next query only.
Do not output a probe query, a fallback query, and a final query together.

### 5. Prove row scope before field scope

If row scope is not proven, field failures are ambiguous.

Interpretation:
- wrong row count means row-gate failure
- right row count with null fields means supplier failure

### 6. Do not invent unproven grammar

Use only syntax already verified in this build.

Safe examples:
- `ORDER BY node_id`
- `attributes.class CONTAINS '...'`
- `EXISTS(descendant WHERE tag = 'a' AND href IS NOT NULL)`
- top-level `PROJECT(row) AS (...)`
- `WITH ...`
- `CROSS JOIN LATERAL (SELECT self FROM doc ...)`
- `LEFT JOIN` helper relations on row id

### 7. Obey literal user constraints

If the user says:
- “CTE only”
- “no PROJECT”
- “no FLATTEN”

obey that literally.
Do not return a hybrid query.

### 8. Do not mix helper-row CTEs with shaped extraction

`PROJECT(...)` and `FLATTEN(...)` are allowed inside CTEs, but only as pure shaped relations.

Do not do this:
- `SELECT row_id, PROJECT(row) AS (...) ...`
- `SELECT row_id, FLATTEN(row) AS (...) ...`

Verified behavior:
- mixed helper + shaped CTEs can lose row identity or produce misleading null behavior

Safe split:
- helper CTEs carry row ids and scalar helper values
- shaped CTEs are pure extracted relations

### 9. Keep outer row logic separate from field logic

Outer `WHERE` controls row survival.
Field expressions choose suppliers for kept rows.

Do not blur those two jobs.

### 10. Nulls are signal

Do not dismiss nulls as noise.

Interpret nulls precisely:
- all rows wrong -> row-gate issue
- correct rows, one field null -> supplier issue
- some rows good, some null -> mixed family or unstable supplier
- helper CTE works but top-level field extraction fails -> supplier must be materialized explicitly

## Strong Heuristics

### 11. Prefer explicit supplier materialization on hard pages

When field extraction is unstable:
- stop broad guessing with `TEXT(...)` or `ATTR(...)`
- switch to explicit helper relations

Reference pattern:
- `r_rows`
- `CROSS JOIN LATERAL`
- `SELECT self`
- `parent_id`
- `tag`
- `href IS NOT NULL`
- `sibling_pos`
- `LEFT JOIN` helper relations back on row id

### 12. Prefer structural facts over family slogans

`slot:a[href]` is useful, but weaker than:
- `parent_id = row_id`
- `sibling_pos = n`
- exact child tag
- direct-child class predicate

Use concrete structure when extracting fields.

### 13. Prefer positive structure first

Do not introduce negation casually.

If `NOT EXISTS(...)` or other negation is not already proven for the current build and page shape, prefer positive structural gates first.

### 14. Escalate one artifact level only

Escalation order:

1. `--families-compact`
2. `--families`
3. `--skeleton`
4. targeted subtree or HTML slice
5. full HTML

Do not jump straight to raw HTML.

### 15. Keep prompts narrow

The prompt should contain only:
- user goal
- one current artifact
- one tiny syntax pack for the current step
- one next query request

Do not dump:
- full HTML
- full docs
- full history
- several family choices
- several fallback queries

## Anti-Patterns

Never:
- suggest a large production query before row scope is proven
- treat compact family output as a DOM substitute
- give multiple next queries in one answer
- ignore explicit user constraints
- assume top-level `ATTR(a, href)` is equivalent to explicit supplier materialization
- repeat the same failed query shape with small wording changes
- use mixed row-id helper CTEs with `PROJECT(...)` or `FLATTEN(...)`

## Default Recommendation Path

Use this sequence:

1. choose one family from compact mode
2. do a row check
3. do a row gate
4. if extraction is simple, test one field
5. if field extraction is unstable, switch to explicit helper CTE materialization
6. only after several fields are proven, scale to full extraction
7. if compact mode is too lossy, escalate to `--families`, not raw HTML

## Practical Summary

The most reliable agent behavior is:
- small artifact
- one family
- one query
- local validation
- explicit structure
- explicit escalation

That is the highest-hit-rate path we have verified so far.
