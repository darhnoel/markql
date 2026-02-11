# Chapter 9: PROJECT

## TL;DR
`PROJECT` is the schema contract of MarkQL: explicit field names, explicit supplier logic, predictable null behavior, and stable extraction intent.

## What is `PROJECT`?
`PROJECT(base_tag) AS (field: expr, ...)` is MarkQL’s schema-construction operator. It evaluates named expressions for each kept row, with each expression scoped under the row node selected by `base_tag`. In practical terms, `PROJECT` is where you stop “collecting text” and start “defining a contract.”

`PROJECT` matters because production extraction is not about finding *some* value; it is about returning the *right* value in the *right* column every time. `FLATTEN` is useful when discovering layout. `PROJECT` is useful when the output must be stable, explainable, and reviewable by other engineers.

This chapter can feel new at first, even if you already know SQL. The reason is not syntax complexity. The reason is scope complexity: there are two `WHERE` universes and they do different jobs. Most confusion disappears once you can narrate stage 1 and stage 2 separately.

> ### Note: Two-stage evaluation is the core of PROJECT
> `PROJECT` does not replace outer filtering. It runs *after* row filtering. Keep this invariant in your head:
>
> 1. Outer `WHERE` decides whether row `R` exists.
> 2. `PROJECT` fields decide values for row `R`.
>
> If a field cannot find a supplier node, that field becomes `NULL`; row `R` still exists unless outer `WHERE` removed it.

## Rules
- Outer `WHERE` controls row existence.
- Field-level `WHERE` inside `TEXT/ATTR` controls supplier selection.
- Missing supplier => field value `NULL`, row remains.
- Use `EXISTS(...)` in outer `WHERE` when supplier existence should gate rows.
- Use `FIRST_TEXT`, `LAST_TEXT`, or indexed `TEXT(..., n)` for repeated nodes.
- Use `COALESCE` and `CASE` to turn optional HTML into stable columns.

## Scope

### Scope diagram A: row selection vs field selection

```text
Stage 1: row selection
  doc row stream -> apply outer WHERE -> kept rows K

Stage 2: field evaluation
  for each row R in K:
    evaluate field_1 under scope(R)
    evaluate field_2 under scope(R)
    ...
```

### Scope diagram B: supplier selection within one field

```text
R = current kept row (e.g., <section data-kind='flight'>)

TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')
  candidates under R: all span descendants
  predicate filter: DIRECT_TEXT(span) LIKE '%stop%'
  chosen supplier: first matching candidate (unless FIRST/LAST/index overrides)
  returned value: text(supplier) or NULL
```

## Listing 9-1: Baseline PROJECT extraction
Fixture: `docs/fixtures/basic.html`

Query:

```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  stops: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%'),
  price: TEXT(span WHERE attributes.role = 'text')
)
FROM doc
WHERE tag = 'section'
ORDER BY node_id;
```

<!-- VERIFY: ch09-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stops: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%'), price: TEXT(span WHERE attributes.role = 'text')) FROM doc WHERE tag = 'section' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","stops":"1 stop","price":"¥12,300"},
  {"node_id":11,"title":"Osaka","stops":"nonstop","price":"¥8,500"},
  {"node_id":16,"title":"Kyoto Stay","stops":null,"price":null}
]
```

Evaluation trace for row `node_id=16`:
1. Stage 1 kept row 16 because `tag='section'`.
2. Field `title` found supplier `h3` -> `Kyoto Stay`.
3. Field `stops` searched for matching `span` -> none -> null.
4. Field `price` searched for role-text `span` -> none -> null.
5. Row emitted with partial nulls.

That trace is the language contract in action. If you can narrate this trace, you can debug most PROJECT queries without guesswork.

## Listing 9-2: Same field expression, stricter outer gate

Query:

```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')
)
FROM doc
WHERE tag = 'section'
  AND EXISTS(descendant WHERE tag = 'span' AND text LIKE '%stop%')
ORDER BY node_id;
```

<!-- VERIFY: ch09-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')) FROM doc WHERE tag = 'section' AND EXISTS(descendant WHERE tag = 'span' AND text LIKE '%stop%') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","stop_text":"1 stop"},
  {"node_id":11,"title":"Osaka","stop_text":"nonstop"}
]
```

Now row `16` is gone. Why? Not because field extraction changed; because stage 1 changed. This is the single most important debugging lesson in MarkQL.

## Listing 9-3: Stable repeated-node picking (`FIRST`, `LAST`, index)

Query:

```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  first_span: FIRST_TEXT(span),
  second_span: TEXT(span, 2),
  last_span: LAST_TEXT(span)
)
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

<!-- VERIFY: ch09-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), first_span: FIRST_TEXT(span), second_span: TEXT(span, 2), last_span: LAST_TEXT(span)) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","first_span":"1 stop","second_span":"2 hr 30 min","last_span":"¥12,300"},
  {"node_id":11,"title":"Osaka","first_span":"nonstop","second_span":"1 hr 10 min","last_span":"¥8,500"}
]
```

This listing answers a common production problem: repeated tags are normal in real pages. Positional selection (`FIRST`, `LAST`, `n`) makes selection deterministic without inventing brittle pseudo-selectors.

## Listing 9-4: Null-stability with `COALESCE`

Query:

```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  subtitle: TEXT(p WHERE attributes.class = 'summary'),
  subtitle_final: COALESCE(TEXT(p WHERE attributes.class = 'summary'), 'N/A')
)
FROM doc
WHERE tag = 'section'
ORDER BY node_id;
```

<!-- VERIFY: ch09-listing-4 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), subtitle: TEXT(p WHERE attributes.class = 'summary'), subtitle_final: COALESCE(TEXT(p WHERE attributes.class = 'summary'), 'N/A')) FROM doc WHERE tag = 'section' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","subtitle":null,"subtitle_final":"N/A"},
  {"node_id":11,"title":"Osaka","subtitle":null,"subtitle_final":"N/A"},
  {"node_id":16,"title":"Kyoto Stay","subtitle":"Near station","subtitle_final":"Near station"}
]
```

This is where schema contracts become practical. Consumers downstream should not need to infer whether null means “missing because bug” or “missing because optional.” `COALESCE` lets you encode that policy directly in extraction.

## Listing 9-5: Deliberate failure (`PROJECT` requires `AS (...)`)
Naive query:

```sql
SELECT PROJECT(li) FROM doc WHERE tag='li';
```

<!-- VERIFY: ch09-listing-5-fail -->
```bash
# EXPECT_FAIL: requires AS (alias: expression, ...)
./build/markql --mode plain --color=disabled \
  --query "SELECT PROJECT(li) FROM doc WHERE tag='li';" \
  --input docs/fixtures/products.html
```

Observed error:

```text
Error: Query parse error: PROJECT()/FLATTEN_EXTRACT() requires AS (alias: expression, ...)
```

Why this fails: `PROJECT` is schema-construction syntax, not a scalar function call. Without `AS (...)`, the engine has no declared output fields and cannot build a deterministic schema.

Fix:

<!-- VERIFY: ch09-listing-6 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, PROJECT(li) AS (name: TEXT(h2), note: COALESCE(TEXT(p), 'n/a')) FROM doc WHERE tag='li' ORDER BY node_id;" \
  --input docs/fixtures/products.html
```

Observed output:

```json
[
  {"node_id":3,"name":"Alpha","note":"Fast and light"},
  {"node_id":8,"name":"Beta","note":"n/a"},
  {"node_id":11,"name":"Gamma","note":"Budget"}
]
```

## Before/after diagrams for the core confusion

```text
Before (common but incorrect)
  PROJECT field WHERE decides row inclusion
```

```text
After (correct)
  outer WHERE decides row inclusion
  PROJECT field WHERE decides supplier for one field
```

## Narrative continuity: how this chapter connects backward and forward
- From Chapter 5 (`WHERE`): row gating remains stage 1.
- From Chapter 6 (axes): `EXISTS` is still the structural gate for row inclusion.
- From Chapter 8 (`FLATTEN`): `PROJECT` is the stability-oriented successor when drift appears.
- Into Chapter 10 (NULL): `PROJECT` plus null policy (`COALESCE`, `CASE`) is how you make extraction robust under optional DOM.

If you keep only one sentence from this chapter, keep this one:

**MarkQL extraction correctness is mostly scope correctness, and scope correctness starts with separating stage 1 and stage 2.**

## Common mistakes
- Writing field predicates when the real requirement is row inclusion.  
  Fix: move that condition to outer `WHERE` with `EXISTS(...)` if needed.
- Assuming repeated tags pick the “right one” automatically.  
  Fix: use `FIRST/LAST/index` explicitly.

## Chapter takeaway
`PROJECT` turns extraction from a best-effort traversal into an explicit, maintainable data contract.
