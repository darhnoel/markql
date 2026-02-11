# MarkQL CLI Guide: Intro to Advanced

This guide explains why MarkQL exists, how to use it from the CLI, and how to move from basic to advanced workflows.

## Why MarkQL?

MarkQL is useful when you want SQL-like querying over HTML without building a custom scraper for every page shape.

Use MarkQL when you need to:
- Inspect and extract structured data from static HTML quickly.
- Filter elements by attributes, hierarchy, and text rules.
- Iterate fast in terminal/REPL before writing production automation.
- Export results in machine-friendly formats (JSON list, table rows, CSV, Parquet).

## Core Mental Model

MarkQL treats HTML elements as rows in a node table.

Each row has core fields:
- `node_id`
- `tag`
- `attributes`
- `parent_id`
- `sibling_pos`
- `max_depth`
- `doc_order`
- `source_uri`

Think of it as:
- `SELECT <tag or projected fields>`
- `FROM <html source>`
- `WHERE <filters>`
- optional `LIMIT`, `TO LIST`, `TO TABLE`, `TO CSV`, `TO PARQUET`, `TO JSON`, `TO NDJSON`

For `PROJECT(...)`, keep this exact mental model:
- `PROJECT(base_tag)` chooses row candidates by tag (`PROJECT(document)` behaves like all tags).
- Outer `WHERE` filters those row candidates.
- Field predicates inside `PROJECT(... AS (...))` choose which row-scoped node provides each field value.
- Row scope for field extraction is the row node plus its descendants.

Short version:
> PROJECT picks candidates, outer WHERE filters rows, field WHERE picks values.

Deep explanation:
- [MarkQL deep dive](markql-deep-dive.md)

## CLI Setup

Build:
```bash
./build.sh
```

Run one query:
```bash
./build/markql --query "SELECT a FROM doc WHERE href CONTAINS 'https'" --input ./data/index.html
```

Run REPL:
```bash
./build/markql --interactive --input ./data/index.html
```

Compatibility note:
- `./build/xsql` remains available as a legacy command name.

## Fast Start: 5 Queries

```sql
SELECT div FROM doc LIMIT 5;
```

```sql
SELECT a FROM doc WHERE href CONTAINS 'https';
```

```sql
SELECT a.href FROM doc WHERE rel = 'preload' TO LIST();
```

```sql
SELECT table FROM doc TO TABLE();
```

```sql
SELECT COUNT(a) FROM doc;
```

## Sources

Use different input sources:

```sql
SELECT div FROM document;
SELECT div FROM 'page.html';
SELECT div FROM 'https://example.com';
SELECT div FROM RAW('<div class="x">hello</div>');
```

Alias sources:
```sql
SELECT a FROM document AS d WHERE d.id = 'login';
```

## Filtering with WHERE

Basic operators:
- `=`
- `<>` / `!=`
- `<`, `<=`, `>`, `>=`
- `IN (...)`
- `LIKE` (`%` any sequence, `_` one character)
- `IS NULL` / `IS NOT NULL`
- `~` regex
- `CONTAINS`, `CONTAINS ALL`, `CONTAINS ANY` (attributes)
- `HAS_DIRECT_TEXT`
- `EXISTS(axis [WHERE expr])`

Examples:
```sql
SELECT div FROM doc WHERE id = 'main';
SELECT a FROM doc WHERE href IN ('/a','/b');
SELECT a FROM doc WHERE href ~ '.*\.pdf$';
SELECT div FROM doc WHERE text LIKE '%coupon%';
SELECT div FROM doc WHERE POSITION('coupon' IN LOWER(text)) > 0;
SELECT div FROM doc WHERE attributes IS NULL;
SELECT div FROM doc WHERE div HAS_DIRECT_TEXT 'buy now';
SELECT div FROM doc WHERE EXISTS(child);
SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'h2');
```

SQL-style direct text form (preferred over `HAS_DIRECT_TEXT`):
```sql
SELECT div FROM doc WHERE DIRECT_TEXT(div) LIKE '%buy now%';
```

Current behavior note:
- `LIKE` matching is ASCII case-insensitive in this release.

Reserved keywords used by these features:
- `LIKE`
- `CONCAT`
- `SUBSTRING` / `SUBSTR`
- `LENGTH` / `CHAR_LENGTH`
- `POSITION` / `LOCATE`
- `REPLACE`
- `LOWER` / `UPPER`
- `LTRIM` / `RTRIM`
- `DIRECT_TEXT`
- `CASE` / `WHEN` / `THEN` / `ELSE` / `END`

## Hierarchy (Axes)

Axes let you filter by relationships:
- `parent`
- `child`
- `ancestor`
- `descendant`

Examples:
```sql
SELECT span FROM doc WHERE parent.tag = 'div';
SELECT div FROM doc WHERE descendant.attributes.data-testid = 'review-text';
SELECT a FROM doc WHERE ancestor.id = 'content';
```

Important parser detail for axis attributes:
- Use `child.attributes.foo`, `parent.attributes.foo`, `descendant.attributes.foo`, etc.
- In this branch, shorthand like `child.foo` may fail parse.

`EXISTS` predicate:
- Syntax: `EXISTS(self|parent|child|ancestor|descendant [WHERE <expr>])`
- `EXISTS(axis)` checks whether at least one node exists on that axis.
- `EXISTS(axis WHERE ...)` evaluates `<expr>` on each axis node and returns true if any one node matches.
- Conditions inside `EXISTS(... WHERE ...)` are applied to the same axis node.

Examples:
```sql
SELECT div FROM doc WHERE EXISTS(child);
SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'h2');
SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'span' AND attributes.class = 'price');
```

## Projections

Project fields:
```sql
SELECT link.href FROM doc;
SELECT div(node_id, tag, parent_id) FROM doc;
```

Project functions:
```sql
SELECT inner_html(div) FROM doc WHERE id = 'card';
SELECT raw_inner_html(div) FROM doc WHERE id = 'card';
SELECT trim(inner_html(div)) FROM doc WHERE id = 'card';
SELECT text(div) FROM doc WHERE attributes.class = 'summary';
SELECT lower(replace(trim(text(div)), ' ', '-')) AS slug FROM doc WHERE attributes.class = 'summary';
```

Notes:
- `TEXT()` and `INNER_HTML()` require a `WHERE` with a non-tag filter.
- `INNER_HTML()` is minified by default; use `RAW_INNER_HTML()` for raw spacing.
- `LENGTH()/CHAR_LENGTH()` currently count UTF-8 bytes.

## SQL String Functions

Available in `SELECT`, `WHERE`, and inside `PROJECT(...)` expressions:
- `CONCAT(a, b, ...)`
- `SUBSTRING(str, start, len)` and `SUBSTR(...)`
- `LENGTH(str)` and `CHAR_LENGTH(str)` (UTF-8 byte length)
- `POSITION(substr IN str)` and `LOCATE(substr, str[, start])`
- `REPLACE(str, from, to)`
- `LOWER(str)`, `UPPER(str)`
- `LTRIM(str)`, `RTRIM(str)`, `TRIM(str)`
- `DIRECT_TEXT(tag)`

Examples:
```sql
SELECT CONCAT(attributes.class, '-x') AS label
FROM doc
WHERE tag = 'div';
```

```sql
SELECT SUBSTRING(TRIM(TEXT(div)), 1, 10) AS preview
FROM doc
WHERE attributes.id = 'card';
```

## FLATTEN_TEXT / FLATTEN

`FLATTEN_TEXT` is for extracting ordered text slices from descendant nodes.

```sql
SELECT FLATTEN_TEXT(div) AS (date, body)
FROM doc
WHERE attributes.class = 'review'
  AND descendant.attributes.data-testid CONTAINS ANY ('review-date', 'review-text');
```

Alias:
```sql
SELECT FLATTEN(div) AS (value) FROM doc WHERE descendant.tag = 'span';
```

Common mistakes:
- `FLATTENT(...)` is invalid. Use `FLATTEN_TEXT(...)` or `FLATTEN(...)`.
- If validation requires aliases, use `AS (col1, col2, ...)`.

## PROJECT

`PROJECT` is for stable field extraction per base row using expression mapping.

Supported expression forms:
- `TEXT(tag WHERE <predicate>)`
- `ATTR(tag, attr WHERE <predicate>)`
- `TEXT(..., <n>)` / `ATTR(..., <n>)` for 1-based stable selection
- `FIRST_TEXT(...)`, `LAST_TEXT(...)`, `FIRST_ATTR(...)`, `LAST_ATTR(...)`
- `COALESCE(expr1, expr2, ...)`
- `DIRECT_TEXT(tag [WHERE <predicate>])`
- `CASE WHEN <boolean_expr> THEN <value_expr> [ELSE <value_expr>] END`
- SQL string functions (for example `LOWER(REPLACE(TRIM(TEXT(h2)), ' ', '-'))`)
- Alias references to previous fields in the same `AS (...)` block
- Top-level comparisons on expressions (for example `POSITION('coupon' IN LOWER(TEXT(li))) > 0`)

Example:
```sql
SELECT tr.node_id,
PROJECT(tr) AS (
  period: TEXT(td WHERE sibling_pos = 1),
  pdf_direct: COALESCE(
    ATTR(a, href WHERE parent.sibling_pos = 3 AND href CONTAINS '.pdf'),
    TEXT(td WHERE sibling_pos = 3)
  ),
  excel_direct: COALESCE(
    ATTR(a, href WHERE parent.sibling_pos = 5 AND href CONTAINS '.xlsx'),
    TEXT(td WHERE sibling_pos = 5)
  )
)
FROM doc
WHERE EXISTS(child WHERE tag = 'td');
```

Notes:
- `AS (...)` is required and must use `alias: expression`.
- `COALESCE` returns the first non-NULL, non-blank extracted value.
- Use `HAS_DIRECT_TEXT` as an operator (`td HAS_DIRECT_TEXT '2025'`), not as a field.
- Selector indexes are 1-based (`TEXT(..., 2)` is the second match). Out-of-range indexes return `NULL`.
- `FLATTEN_EXTRACT(...)` is kept as a compatibility alias.
- Fields are evaluated left-to-right; later aliases can reference earlier ones.

## Output Modes

### TO LIST
Single projected column to JSON list:
```sql
SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST();
```

### TO TABLE
Extract HTML table rows:
```sql
SELECT table FROM doc TO TABLE();
SELECT table FROM doc TO TABLE(HEADER=OFF);
SELECT table FROM doc WHERE id = 'stats' TO TABLE(EXPORT='stats.csv');
```

### TO CSV / TO PARQUET
```sql
SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL TO CSV('links.csv');
SELECT * FROM doc TO PARQUET('nodes.parquet');
```
By default, exported column names are normalized to identifier-safe names
(for example `data-id` -> `data_id`).

### TO JSON / TO NDJSON
```sql
SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL TO JSON('links.json');
SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL TO NDJSON('links.ndjson');
```
Both also accept empty destination (`TO JSON()` / `TO NDJSON()`) to stream to stdout.

## REPL Workflow

Useful commands:
- `.help`
- `.load <path|url> [--alias <name>]`
- `.mode duckbox|json|plain`
- `.set colnames raw|normalize`
- `.display_mode more|less`
- `.max_rows <n|inf>`
- `DESCRIBE LAST`
- `.summarize [doc|alias|path|url]`
- `.reload_config`
- `.quit`

Column-name modes:
- `normalize` (default): use identifier-safe output headers/keys.
- `raw`: keep original projected names.
- `DESCRIBE LAST`: show `raw_name` and `output_name` for the previous query.

Vim navigation mode:
- Default editor mode is normal.
- Press `Esc` to switch into Vim normal mode.
- In Vim mode, press `Esc` to toggle between `vim:edit` and `vim:normal`, and back to normal mode.
- Prompts:
  - normal: `markql> `
  - vim normal: `markql (vim:normal)> `
  - vim edit: `markql (vim:edit)  > ` (padded to keep width aligned)
- Vim keys: `h/j/k/l`, `i/a/I/A`, `o/O`.

Great iterative pattern:
1. `.load` input
2. Start with `SELECT * ... LIMIT 5`
3. Add `WHERE` filters
4. Add projections/functions
5. Export with `TO LIST/TO CSV/TO TABLE`

## Practical Advanced Use Cases

### 1) Review extraction
```sql
SELECT div.data-review-id, FLATTEN_TEXT(div) AS (review_text)
FROM doc
WHERE attributes.class = 'review'
  AND descendant.attributes.data-testid = 'review-text';
```

### 2) Navigation audit
```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE ancestor.id = 'navbar' AND href IS NOT NULL
TO CSV('nav_links.csv');
```

### 3) Content block quality checks
```sql
SELECT section
FROM doc
WHERE attributes.class CONTAINS 'content'
  AND descendant.tag IN ('h1','h2','p');
```

### 4) Table extraction for analytics
```sql
SELECT table FROM doc WHERE id = 'report' TO TABLE(EXPORT='report.csv');
```

## Troubleshooting

If you get parse errors:
- `Expected FROM`: missing `FROM ...`.
- `Expected attributes, tag, text... after child`: use `child.attributes.<name>`.
- `FLATTEN_TEXT() requires AS (...)`: add column aliases.
- `TEXT()/INNER_HTML() requires a non-tag filter`: add attribute/parent style predicate.

If no rows return:
- Check whether attribute names are exact (`data-testid` vs `data-test-id`).
- Use broader filter first (`CONTAINS`) then narrow.
- Test with `LIMIT` and simpler predicates.

## Self-Discovery Commands

Use built-in metadata queries while learning:

```sql
SHOW FUNCTIONS;
SHOW AXES;
SHOW OPERATORS;
DESCRIBE doc;
DESCRIBE language;
```
