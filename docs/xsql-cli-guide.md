# XSQL CLI Guide: Intro to Advanced

This guide explains why XSQL exists, how to use it from the CLI, and how to move from basic to advanced workflows.

## Why XSQL?

XSQL is useful when you want SQL-like querying over HTML without building a custom scraper for every page shape.

Use XSQL when you need to:
- Inspect and extract structured data from static HTML quickly.
- Filter elements by attributes, hierarchy, and text rules.
- Iterate fast in terminal/REPL before writing production automation.
- Export results in machine-friendly formats (JSON list, table rows, CSV, Parquet).

## Core Mental Model

XSQL treats HTML elements as rows in a node table.

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
- optional `LIMIT`, `TO LIST`, `TO TABLE`, `TO CSV`, `TO PARQUET`

## CLI Setup

Build:
```bash
./build.sh
```

Run one query:
```bash
./build/xsql --query "SELECT a FROM doc WHERE href CONTAINS 'https'" --input ./data/index.html
```

Run REPL:
```bash
./build/xsql --interactive --input ./data/index.html
```

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
- `IN (...)`
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
SELECT div FROM doc WHERE attributes IS NULL;
SELECT div FROM doc WHERE div HAS_DIRECT_TEXT 'buy now';
SELECT div FROM doc WHERE EXISTS(child);
SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'h2');
```

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
```

Notes:
- `TEXT()` and `INNER_HTML()` require a `WHERE` with a non-tag filter.
- `INNER_HTML()` is minified by default; use `RAW_INNER_HTML()` for raw spacing.

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

## REPL Workflow

Useful commands:
- `.help`
- `.load <path|url> [--alias <name>]`
- `.mode duckbox|json|plain`
- `.display_mode more|less`
- `.max_rows <n|inf>`
- `.summarize [doc|alias|path|url]`
- `.reload_config`
- `.quit`

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
