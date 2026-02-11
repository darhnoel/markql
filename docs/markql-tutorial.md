# MarkQL Tutorial

This is the canonical tutorial for MarkQL.

If you only read one top-level document in this repository, read this one.

## What MarkQL Is

MarkQL is a SQL-style query language for HTML. It treats parsed DOM nodes as rows and lets you filter and project them using familiar query structure: `SELECT ... FROM ... WHERE ...`.

MarkQL exists to solve a practical extraction problem: you usually do not fail at finding one value once; you fail at keeping extraction stable after page updates. MarkQL addresses that by making row selection and field extraction explicit instead of mixing them in ad-hoc traversal code.

This model can feel unusual at first if you are used to one-pass scraping scripts. That is normal. Once the row/field separation clicks, debugging gets much faster because each query has a clear failure point.

## Note: The Core Mental Model

> ### Note
> MarkQL runs in two stages:
>
> 1. Outer `WHERE` filters row candidates.
> 2. Field expressions compute output values for each kept row.
>
> The most important consequence is:
> - Outer `WHERE` decides whether a row exists in output.
> - Field predicates decide which node supplies a field value.
>
> If a field has no matching supplier, that field becomes `NULL`. The row remains unless outer `WHERE` excludes it.

## Rules To Remember

1. Probe candidate rows first. Do not start with a large `PROJECT(...)`.
2. Use durable anchors (`data-*`, structural patterns) before cosmetic class-only filters.
3. Add `EXISTS(...)` guards when cards can be incomplete variants.
4. Treat repeated blocks explicitly with `FIRST_...` and `LAST_...`.
5. Normalize output values in-query (`TRIM`, `REPLACE`) before export.
6. Keep extraction functions (`TEXT`, `INNER_HTML`, `RAW_INNER_HTML`) behind an outer `WHERE` clause.

## Scope Diagram

```text
doc
├─ node #A (candidate row) -- outer WHERE --> keep or drop
│   ├─ descendant d1
│   ├─ descendant d2
│   └─ descendant d3
└─ node #B (candidate row) -- outer WHERE --> keep or drop

For each kept row:
  PROJECT(row) fields choose suppliers from [row + descendants]
```

## Build and First Command

Build:

```bash
./build.sh
```

**Listing 1: Verify the CLI and inspect rows**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT section(node_id, tag) FROM doc WHERE attributes.class CONTAINS 'result' LIMIT 5;"
```

Observed output:

```text
node_id  tag
6        section
11       section
16       section
Rows: 3
```

Why this first listing matters: it confirms parsing works and shows that your row anchor (`section` with `class` containing `result`) is real before field extraction begins.

## Query Shape

General structure:

```sql
SELECT <items>
FROM <source>
[WHERE <predicate>]
[ORDER BY <field> [ASC|DESC]]
[LIMIT n]
[TO LIST() | TO TABLE() | TO CSV('file.csv') | TO PARQUET('file.parquet') | TO JSON(['file.json']) | TO NDJSON(['file.ndjson'])];
```

Common sources:

```sql
FROM doc
FROM document
FROM 'local.html'
FROM 'https://example.com'
FROM RAW('<div>...</div>')
```

## From Row Probe To Real Extraction

**Listing 2: A minimal `PROJECT` extraction**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT section.node_id, PROJECT(section) AS ( city: TEXT(h3), stops: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%'), price_text: TEXT(span WHERE attributes.role = 'text') ) FROM doc WHERE attributes.class CONTAINS 'result' AND attributes.data-kind = 'flight' ORDER BY node_id;"
```

Observed output:

```text
node_id  city   stops    price_text
6        Tokyo  1 stop   ¥12,300
11       Osaka  nonstop  ¥8,500
Rows: 2
```

What happened:

1. Outer `WHERE` kept only flight cards.
2. For each row, `city`, `stops`, and `price_text` were computed independently.
3. `DIRECT_TEXT(span) LIKE '%stop%'` selected the stop label supplier node without relying on brittle positional assumptions.

## Deliberate Failure: Field Extraction Guard

**Listing 3: Failing query**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT TEXT(div) FROM doc;"
```

Observed error:

```text
Error: TEXT()/INNER_HTML()/RAW_INNER_HTML() requires a WHERE clause
```

Why this fails: this branch requires extraction functions to run under a query-level `WHERE` guard. This prevents accidental whole-document text extraction.

**Listing 4: Corrected query**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT TEXT(p) FROM doc WHERE attributes.class CONTAINS 'summary';"
```

Observed output:

```text
text
Near station
Rows: 1
```

The fix is not syntax decoration. It forces explicit extraction scope.

## Deliberate Failure: Row Filtering vs Field Selection

**Listing 5: Naive row filter (keeps incomplete variant)**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT section.node_id, PROJECT(section) AS ( name: TEXT(h3), price_text: TEXT(span WHERE attributes.role = 'text') ) FROM doc WHERE attributes.class CONTAINS 'result' ORDER BY node_id;"
```

Observed output:

```text
node_id  name        price_text
6        Tokyo       ¥12,300
11       Osaka       ¥8,500
16       Kyoto Stay  NULL
Rows: 3
```

Why this happens: outer `WHERE` admitted both flight and hotel cards. The hotel row has no price supplier node, so only that field becomes `NULL`.

**Listing 6: Corrected with row-quality guard**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(descendant WHERE attributes.role = 'text') ORDER BY node_id;"
```

Observed output:

```text
node_id
6
11
Rows: 2
```

This is the key pattern: use `EXISTS(...)` in outer `WHERE` to keep rows that have required structural evidence.

## FLATTEN vs PROJECT

`FLATTEN` is fast for prototyping regular blocks. `PROJECT` is better when schema stability matters.

**Listing 7: FLATTEN on semi-regular items**

```bash
./build/markql --input docs/fixtures/products.html --query "SELECT li.node_id, FLATTEN(li) AS (name, summary, lang1, lang2) FROM doc WHERE attributes.class = 'item' ORDER BY node_id;"
```

Observed output:

```text
node_id  name   summary         lang1  lang2
3        Alpha  Fast and light  EN     JP
8        Beta   EN              NULL   NULL
11       Gamma  Budget          EN     TH
Rows: 3
```

Interpretation:

- Fast and compact for exploration.
- On irregular rows (`Beta`), one positional slot shifts and semantic meaning drifts.
- This is exactly when you should switch to explicit `PROJECT(...)`.

**Listing 8: Stable mapping with PROJECT**

```bash
./build/markql --input docs/fixtures/products.html --query "SELECT li.node_id, PROJECT(li) AS ( title: TEXT(h2), summary: TEXT(p), language_primary: FIRST_TEXT(span), language_secondary: LAST_TEXT(span) ) FROM doc WHERE attributes.class = 'item' ORDER BY node_id;"
```

Observed output:

```text
node_id  title  summary         language_primary  language_secondary
3        Alpha  Fast and light  EN                JP
8        Beta   NULL            EN                EN
11       Gamma  Budget          EN                TH
Rows: 3
```

Notice that missing values are explicit (`NULL`) rather than accidental column drift.

## String Operations In Real Queries

MarkQL supports SQL-style string operators and functions in `SELECT`, outer `WHERE`, and `PROJECT` fields.

Examples include:

- `LIKE` (`%`, `_`)
- `CONCAT`
- `SUBSTRING` / `SUBSTR`
- `LENGTH` / `CHAR_LENGTH`
- `POSITION` / `LOCATE`
- `REPLACE`
- `LOWER` / `UPPER`
- `LTRIM` / `RTRIM` / `TRIM`
- `DIRECT_TEXT`
- `CASE WHEN ... THEN ... ELSE ... END`

**Listing 9: Text normalization for numeric export**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT section.node_id, PROJECT(section) AS ( price_text: TEXT(span WHERE attributes.role = 'text'), price_num: TRIM(REPLACE(REPLACE(TEXT(span WHERE attributes.role = 'text'), '¥', ''), ',', '')) ) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;"
```

Observed output:

```text
node_id  price_text  price_num
6        ¥12,300     12300
11       ¥8,500      8500
Rows: 2
```

**Listing 10: CASE expression**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT CASE WHEN tag = 'section' THEN 'card' ELSE 'other' END FROM doc WHERE tag = 'section' LIMIT 2;"
```

Observed output:

```text
case
card
card
Rows: 2
```

## JSON and NDJSON Export

**Listing 11: JSON array**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT a(href, rel) FROM doc WHERE tag = 'a' ORDER BY node_id TO JSON();"
```

Observed output:

```json
[{"href":"/home","rel":"nav"},{"href":"/about","rel":"nav"}]
```

**Listing 12: NDJSON (one object per line)**

```bash
./build/markql --input docs/fixtures/basic.html --query "SELECT a(href, rel) FROM doc WHERE tag = 'a' ORDER BY node_id TO NDJSON();"
```

Observed output:

```json
{"href":"/home","rel":"nav"}
{"href":"/about","rel":"nav"}
```

## RAW Source Example

**Listing 13: Querying inline HTML with `RAW(...)`**

```bash
./build/markql --query "SELECT a(href) FROM RAW('<div><a href=\"/x\">X</a><a href=\"/y\">Y</a></div>') WHERE tag = 'a' ORDER BY node_id TO JSON();"
```

Observed output:

```json
[{"href":"/x"},{"href":"/y"}]
```

## Practical Troubleshooting Checklist

When a query fails or returns weak output:

1. Run a row-only probe first (`node_id`, `tag`, `max_depth`).
2. Verify the outer `WHERE` selects the right entity type.
3. Add `EXISTS(...)` for mandatory descendants.
4. Evaluate each `PROJECT` field independently.
5. Normalize strings only after supplier matching is correct.
6. Export to JSON/NDJSON only after schema is stable.

## Final Takeaway

MarkQL is easiest when you treat extraction as two clear decisions:

1. Keep the right rows.
2. Compute each column from explicit suppliers.

Once you enforce that discipline, query maintenance becomes a routine edit instead of a full scraper rewrite.

## Next

- Chapter-based path: `book/SUMMARY.md`
- Case studies: `case-studies/README.md`
