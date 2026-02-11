# Chapter 1: Why MarkQL

## What is MarkQL?
MarkQL is a SQL-shaped query language for HTML DOM data where each DOM node is treated as a row in a stream, and selectors, predicates, and extraction functions operate on that row stream with explicit scope rules. In concrete terms, `FROM doc` means “iterate nodes”, `WHERE` means “keep or discard rows”, and projection functions such as `TEXT`, `ATTR`, `FLATTEN`, and `PROJECT` mean “materialize values from each kept row”.

MarkQL matters because scraping pipelines usually fail in predictable ways: fragile class selectors, unclear scope semantics, and unstable output schema. MarkQL addresses those pains by making traversal and extraction explicit. You can write structure-aware filters (`EXISTS(descendant WHERE ...)`) and then turn each kept row into a stable shape (`PROJECT(base) AS (...)`) with deliberate fallback logic (`COALESCE`, `CASE`).

This can feel unfamiliar at first because it asks you to think in *stages* instead of in one selector expression. Most users initially expect extraction predicates to filter rows directly. With practice, the model becomes natural: first choose rows, then compute fields. Once that click happens, debugging gets dramatically faster.

> ### Note: The prerequisite mental model is “row stream, not text blob”
> If you treat HTML as one giant text blob, every query feels like substring guessing. MarkQL deliberately avoids that. It parses nodes, keeps node identity (`node_id`, `parent_id`, `doc_order`), and evaluates predicates against row metadata and axis-relative nodes. This is why a MarkQL query can explain itself: you can inspect which rows were kept, then inspect how each field was selected. That observability is the core tradeoff.

## Rules
- A query always starts with row production: `FROM <source>`.
- Outer `WHERE` decides whether a row exists in output at all.
- Projection expressions decide values for rows that already survived.
- Prefer structural predicates (`EXISTS`, axes) before brittle class-name matching.
- Keep early queries small with `LIMIT` so you can verify assumptions quickly.

## Scope
The first scope in MarkQL is the **row scope**: the current row node and its metadata.

```text
row stream from doc
  -> row #0 (html)
  -> row #1 (body)
  -> row #2 (main)
  -> ...
```

The second scope is **expression scope**, used during extraction on a kept row.

```text
kept row node N
  -> evaluate each SELECT / PROJECT expression for N
  -> expression may inspect N, N.parent, N.child, N.descendant
```

## Listing 1-1: Start by seeing rows, not guessed values
Fixture: `docs/fixtures/basic.html`

```html
<main id="content">
  <nav>
    <a href="/home" rel="nav">Home</a>
    <a href="/about" rel="nav">About</a>
  </nav>
</main>
```

Query:

```sql
SELECT * FROM doc LIMIT 3;
```

<!-- VERIFY: ch01-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT * FROM doc LIMIT 3;" \
  --input docs/fixtures/basic.html
```

Observed output (trimmed):

```json
[
  {"node_id":0,"tag":"html","parent_id":null,...},
  {"node_id":1,"tag":"body","parent_id":0,...},
  {"node_id":2,"tag":"main","parent_id":1,...}
]
```

This listing is intentionally boring. In MarkQL, boring first queries are a feature. They give you a factual map of row identity and traversal order before you write logic. Most extraction bugs start when users skip this map and jump directly to assumptions.

The second reason this listing matters is confidence. When you can see row metadata directly, you can reason about `parent`, `descendant`, and `EXISTS` behavior from evidence rather than guesswork. This chapter’s core thesis is that MarkQL is predictable because it is explicit.

## Listing 1-2: A deliberate failure (`Expected FROM`)
Naive query:

```sql
select div(data-id) as data_id from doc;
```

<!-- VERIFY: ch01-listing-2-fail -->
```bash
# EXPECT_FAIL: Expected FROM
./build/markql --mode plain --color=disabled \
  --query "select div(data-id) as data_id from doc;" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: Query parse error: Expected FROM
```

Why this fails: current MarkQL grammar does not support SQL `AS` aliasing in that form. The parser reads tokens and expects a valid `SELECT ... FROM ...` projection grammar. When it sees unsupported shape, it recovers at the `FROM` checkpoint and reports the nearest expectation.

Fix by using supported projection forms.

## Listing 1-3: Correct row-oriented extraction flow
Query:

```sql
SELECT a FROM doc WHERE href IS NOT NULL ORDER BY node_id;
```

<!-- VERIFY: ch01-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT a FROM doc WHERE href IS NOT NULL ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output (trimmed):

```json
[
  {"node_id":4,"tag":"a","attributes":{"href":"/home","rel":"nav"},...},
  {"node_id":5,"tag":"a","attributes":{"href":"/about","rel":"nav"},...}
]
```

Now the narrative should feel concrete: we started with rows, then filtered rows, then inspected resulting rows. That progression is not ceremony; it is the foundation for everything in later chapters. When we introduce `PROJECT`, you will still be using this same logic, only with richer field expressions.

## Before/after diagram: from guessing to staged reasoning

```text
Before (typical scraping guess)
  "I need text that looks like price"
      |
      v
  fragile selector + trial/error
```

```text
After (MarkQL staged loop)
  row map -> row filter -> field extraction -> export
```

By the end of this book, that second loop should feel as routine as writing a `SELECT ... WHERE ...` in a relational database.
