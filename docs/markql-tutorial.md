# MarkQL CLI Practical Usage Guide for Beginners

## Getting started

**Why MarkQL**  
MarkQL is useful when you want SQL-like querying over HTML without building a custom scraper for every page shape. It helps you inspect and extract structured data from static HTML, filter elements by attributes and hierarchy, iterate fast in a terminal/REPL, and export results in machine-friendly formats like JSON lists, table rows, CSV, and Parquet.

**Quick Start (build + first 3 commands)**

Build MarkQL:

```bash
./build.sh
```

Run a first query (sanity check that it works):

```bash
./build/markql --query "SELECT div FROM doc LIMIT 5;" --input ./data/index.html
```

Find links that look like real URLs:

```bash
./build/markql --query "SELECT a FROM doc WHERE href CONTAINS 'https';" --input ./data/index.html
```

Start the interactive REPL (best way to learn fast):

```bash
./build/markql --interactive --input ./data/index.html
```

Compatibility note:
- `./build/xsql` remains available as a legacy command name.

## Mental model and syntax

**Mental Model (HTML as rows, fields, FROM/WHERE/SELECT/TO)**

Think: “HTML elements become rows.”

MarkQL treats HTML elements as rows in a node table. Each element row has core fields like:

- `node_id`
- `tag`
- `attributes`
- `parent_id`
- `sibling_pos`
- `max_depth`
- `doc_order`
- `source_uri`

A query is the same mental flow you already know from SQL:

- `FROM` picks the HTML source (file, URL, raw HTML string).
- `SELECT` picks *what to return* (tags, fields, or functions).
- `WHERE` filters down to the nodes you want.
- `TO ...` controls output format (list, table extraction, CSV, Parquet).
- `LIMIT` keeps output small while you explore.

Start with a broad query:

```sql
SELECT * FROM doc LIMIT 5;
```

Then narrow with `WHERE`:

```sql
SELECT a FROM doc WHERE href CONTAINS 'https' LIMIT 20;
```

Then project only what you need:

```sql
SELECT a.href FROM doc WHERE href CONTAINS 'https' TO LIST();
```

**Syntax Cheatsheet**

Core query shapes:

```sql
SELECT <select_item>[, <select_item> ...]
FROM <source>
[WHERE <expr>]
[ORDER BY <field> [ASC|DESC][, ...]]
[LIMIT <number>]
[TO LIST() | TO TABLE(...) | TO CSV('file.csv') | TO PARQUET('file.parquet') | TO JSON(['file.json']) | TO NDJSON(['file.ndjson'])];
```

`ORDER BY` is supported. In current runtime behavior, ordering is applied by:
`node_id`, `tag`, `text`, `parent_id`, `max_depth`, `doc_order`.
For `SUMMARIZE(*)`, `ORDER BY` supports only `tag` or `count`.

Show/describe (self-discovery):

```sql
SHOW FUNCTIONS;
SHOW AXES;
SHOW OPERATORS;

DESCRIBE doc;
DESCRIBE document;
DESCRIBE language;
```

Common sources (`FROM ...`):

```sql
SELECT div FROM doc;
SELECT div FROM document;

SELECT div FROM 'page.html';
SELECT div FROM 'https://example.com';

SELECT div FROM RAW('<div class="x">hello</div>');
```

`FRAGMENTS(...)` is a supported source form that builds a temporary document by concatenating HTML fragments.  
Use either:

```sql
FROM FRAGMENTS(RAW('<ul><li>1</li><li>2</li></ul>')) AS frag
```

or a subquery that returns a single HTML string column (typically `inner_html(...)`):

```sql
FROM FRAGMENTS(SELECT inner_html(ul) FROM doc WHERE id = 'menu') AS frag
```

Important constraints:
- `FRAGMENTS` subquery must return one HTML string column.
- `FRAGMENTS` subquery cannot use file or URL sources directly.

Filters (`WHERE ...`) operators shown in the guide:

- `=`
- `<>` and `!=`
- `<`, `<=`, `>`, `>=`
- `IN (...)`
- `LIKE` (`%` for any sequence, `_` for one char)
- `IS NULL` and `IS NOT NULL`
- `~` (regex)
- `CONTAINS`, `CONTAINS ALL`, `CONTAINS ANY`
- `HAS_DIRECT_TEXT`
- `EXISTS(axis [WHERE expr])`

Examples:

```sql
SELECT div FROM doc WHERE id = 'main';
SELECT a FROM doc WHERE href IN ('/login', '/signup');
SELECT a FROM doc WHERE href ~ '.*\.pdf$';
SELECT div FROM doc WHERE text LIKE '%coupon%';
SELECT div FROM doc WHERE POSITION('coupon' IN LOWER(text)) > 0;
SELECT div FROM doc WHERE attributes IS NULL;
SELECT div FROM doc WHERE div HAS_DIRECT_TEXT 'buy now';
SELECT div FROM doc WHERE EXISTS(child);
SELECT div FROM doc WHERE EXISTS(child WHERE tag = 'h2');
```

Preferred SQL-style equivalent for direct text matching:
```sql
SELECT div FROM doc WHERE DIRECT_TEXT(div) LIKE '%buy now%';
```

Current behavior note:
- `LIKE` matching is ASCII case-insensitive in this release.

Hierarchy axes (relationship filters):

- `parent`
- `child`
- `ancestor`
- `descendant`

Examples:

```sql
SELECT span FROM doc WHERE parent.tag = 'div';
SELECT div FROM doc WHERE descendant.attributes.data-field = 'body';
SELECT a FROM doc WHERE ancestor.id = 'content';
```

`EXISTS` is a predicate form that works with axes:
- `EXISTS(axis)` means at least one node exists on that axis.
- `EXISTS(axis WHERE expr)` means at least one axis node satisfies `expr`.
- The inner `expr` is evaluated against each axis node directly (same-node semantics).

Example:

```sql
SELECT div
FROM doc
WHERE EXISTS(child WHERE tag = 'span' AND attributes.class = 'price');
```

Branch-specific caveat (important):

**Common mistake**
```sql
SELECT div FROM doc WHERE child.foo = 'bar';
```

**Correct form**
```sql
SELECT div FROM doc WHERE child.attributes.foo = 'bar';
```

In this branch, axis attribute access should use `child.attributes.foo`, `parent.attributes.foo`, `descendant.attributes.foo`, and so on. Shorthand like `child.foo` may fail parse.

Projections (return fields instead of whole nodes):

```sql
SELECT link.href FROM doc;
SELECT div(node_id, tag, parent_id) FROM doc;
```

Text/HTML extraction functions:

```sql
SELECT INNER_HTML(div) FROM doc WHERE id = 'card';
SELECT RAW_INNER_HTML(div) FROM doc WHERE id = 'card';
SELECT TRIM(INNER_HTML(div)) FROM doc WHERE id = 'card';
SELECT TEXT(div) FROM doc WHERE attributes.class = 'summary';
```

Important behavior note:

**Common mistake**
```sql
SELECT TEXT(div) FROM doc;
```

**Correct form**
```sql
SELECT TEXT(div) FROM doc WHERE id = 'card';
```

`TEXT()` and `INNER_HTML()` require a `WHERE` clause with a non-tag filter (for example, `id = ...`, `attributes.class = ...`, or an axis predicate).
`INNER_HTML()` returns minified HTML by default; use `RAW_INNER_HTML()` to preserve raw spacing.
`a.text` is not a valid projection form in this branch; use `TEXT(a)` instead.

**Why this rule exists**
- It prevents accidental full-document extraction (for large pages, `TEXT()`/`INNER_HTML()` across all matching nodes can create very large output and high memory use).
- It forces explicit targeting, so extraction queries are intentional and predictable (`id`, `attributes.class`, axis predicates).
- It keeps function semantics clear: text/inner-html extraction must go through `TEXT(...)` / `INNER_HTML(...)` rather than field-style projection like `a.text`.

## Beginner query recipes

These are meant to be copy/paste, then tweak.

**Recipe: preview the document**
```sql
SELECT * FROM doc LIMIT 5;
```

**Recipe: list the first few `<div>` elements**
```sql
SELECT div FROM doc LIMIT 10;
```

**Recipe: find all links**
```sql
SELECT a FROM doc LIMIT 50;
```

**Recipe: get only link targets (hrefs)**
```sql
SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST();
```

**Recipe: find HTTPS links**
```sql
SELECT a.href FROM doc WHERE href CONTAINS 'https' TO LIST();
```

**Recipe: find PDF links using regex**
```sql
SELECT a.href FROM doc WHERE href ~ '.*\.pdf$' TO LIST();
```

**Recipe: find a main container by id**
```sql
SELECT div FROM doc WHERE id = 'main';
```

**Recipe: find nodes by class**
```sql
SELECT div FROM doc WHERE attributes.class = 'summary' LIMIT 20;
```

**Recipe: start broad with class CONTAINS, then tighten**
```sql
SELECT section FROM doc WHERE attributes.class CONTAINS 'content' LIMIT 20;
```

**Recipe: check for missing attributes**
```sql
SELECT div FROM doc WHERE attributes IS NULL LIMIT 20;
```

**Recipe: count anchors**
```sql
SELECT COUNT(a) FROM doc;
```

**Recipe: extract HTML tables into rows**
```sql
SELECT table FROM doc TO TABLE();
```

## Intermediate and advanced recipes

These focus on hierarchy, text flattening, and export workflows.

**Recipe: filter by parent tag**
```sql
SELECT span FROM doc WHERE parent.tag = 'div' LIMIT 50;
```

**Recipe: find links only inside a specific ancestor container**
```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE ancestor.id = 'navbar' AND href IS NOT NULL
TO CSV('nav_links.csv');
```

**Recipe: find nodes that contain a descendant with a specific attribute**
```sql
SELECT div
FROM doc
WHERE descendant.attributes.data-field = 'body'
LIMIT 50;
```

**Recipe: flatten review text from a review block (use `FLATTEN` by default)**
```sql
SELECT FLATTEN(div) AS (date, body)
FROM doc
WHERE attributes.class = 'review'
  AND descendant.attributes.data-field CONTAINS ANY ('date', 'body');
```

`FLATTEN` and `FLATTEN_TEXT` are aliases.  
This tutorial uses `FLATTEN` as the default because it is shorter.  
`FLATTEN_TEXT` is the original explicit function name.

**How `FLATTEN` / `FLATTEN_TEXT` actually works (implementation behavior)**

1. Base rows are chosen by the function tag argument:
```sql
FLATTEN(div)
```
This targets `div` nodes as base rows.  
Special case: if the function tag equals the source alias (or `document`), it matches all tags.

2. `WHERE` has two roles during flatten:
- Non-`descendant.*` predicates filter base rows.
- `descendant.*` predicates filter which descendant nodes contribute flattened values.

3. Supported `descendant.*` filters in flatten mode:
- `descendant.tag` with `=` or `IN`
- `descendant.attributes.<name>` with `=`, `IN`, `CONTAINS`, `CONTAINS ALL`, `CONTAINS ANY`
- `OR` is not allowed when descendant filters are used with flatten.

4. Depth behavior:
- `FLATTEN(tag)` uses all descendant depths.
- `FLATTEN(tag, depth)` uses exact depth from the base node (`0` = base node itself).
- Default-depth mode skips empty extracted text values.

5. Column mapping behavior:
- Flattened values are assigned left-to-right into aliases.
- If aliases are omitted, default single alias is `flatten_text`.
- More values than aliases are truncated.
- Fewer values than aliases leave remaining columns unset (`null` in JSON output).

6. Query-shape constraints:
- Only one flatten function is allowed per query.
- Flatten cannot be combined with aggregates.
- With `TO LIST()`, flatten must resolve to a single projected output column.

This is why optional fields can shift columns.

Example that often looks correct:
```sql
SELECT div.node_id, FLATTEN(div) AS (title, summary)
FROM document
WHERE attributes.class CONTAINS 'card-wrap'
  AND parent.tag = 'li'
  AND descendant.tag IN ('h2', 'p');
```

Example that can drift:
```sql
SELECT div.node_id, FLATTEN(div) AS (title, summary, english, chinese)
FROM document
WHERE attributes.class CONTAINS 'card-wrap'
  AND parent.tag = 'li'
  AND descendant.tag IN ('h2', 'p', 'span');
```

If `summary` is blank for one card, the first `span` value can slide into `summary`,
then `english`/`chinese` shift too. Extra `span` nodes can also disturb mapping.

**Recommended approach for stable columns**
- Use separate queries by semantic selector (for example: title from `h2`, summary from `.summary-text`, language from `.lang-primary`/`.lang-secondary`), then merge by `node_id` in your app.
- Or fetch `INNER_HTML(div)` and parse fields in application code when page structure is inconsistent.

**Common minimal form**
```sql
SELECT FLATTEN(div) FROM doc;
```

This is valid in the current parser and returns one column named `flatten_text`.

**Structured row extraction with `PROJECT`**

Use `PROJECT` when positional flattening is too fragile and you want explicit field mapping.

Supported expression forms:
- `TEXT(tag WHERE <predicate>)`
- `ATTR(tag, attr WHERE <predicate>)`
- `TEXT(..., <n>)` / `ATTR(..., <n>)` for 1-based stable selection
- `FIRST_TEXT(...)`, `LAST_TEXT(...)`, `FIRST_ATTR(...)`, `LAST_ATTR(...)`
- `COALESCE(expr1, expr2, ...)`
- `DIRECT_TEXT(tag [WHERE <predicate>])`
- `CASE WHEN <boolean_expr> THEN <value_expr> [ELSE <value_expr>] END`
- SQL string functions such as `LOWER`, `REPLACE`, `SUBSTRING`, `CONCAT`, `POSITION`
- Alias references to previously defined fields in the same `AS (...)`
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
  pdf_layover: COALESCE(
    ATTR(a, href WHERE parent.sibling_pos = 4 AND href CONTAINS '.pdf'),
    TEXT(td WHERE sibling_pos = 4)
  )
)
FROM doc
WHERE EXISTS(child WHERE tag = 'td');
```

Important syntax details:
- `AS (...)` is required and must use `alias: expression`.
- `HAS_DIRECT_TEXT` is an operator form (`td HAS_DIRECT_TEXT '2025'`), not a projected field.
- `FLATTEN_EXTRACT(...)` is supported as a compatibility alias.
- Selector indexes are 1-based. If index is out of range, the expression returns `NULL`.
- `LENGTH()/CHAR_LENGTH()` currently count UTF-8 bytes.

**SQL string function example**
```sql
SELECT li.node_id,
PROJECT(li) AS (
  title: TRIM(TEXT(h2)),
  slug: LOWER(REPLACE(title, ' ', '-')),
  score_pos: POSITION('score' IN LOWER(TEXT(li)))
)
FROM doc
WHERE EXISTS(descendant WHERE tag = 'h2');
```

**Recipe: flatten generic values (FLATTEN)**
```sql
SELECT FLATTEN(div) AS (value)
FROM doc
WHERE descendant.tag = 'span';
```

**Recipe: export a link table to CSV**
```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE href IS NOT NULL
TO CSV('links.csv');
```

**Recipe: export row objects to JSON / NDJSON**
```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE href IS NOT NULL
TO JSON('links.json');

SELECT a.href, TEXT(a)
FROM doc
WHERE href IS NOT NULL
TO NDJSON('links.ndjson');
```

**Recipe: export the full node table to Parquet**
```sql
SELECT * FROM doc TO PARQUET('nodes.parquet');
```

**Recipe: extract a specific HTML table by id and export**
```sql
SELECT table
FROM doc
WHERE id = 'report'
TO TABLE(EXPORT='report.csv');
```

**Recipe: table extraction without headers**
```sql
SELECT table FROM doc TO TABLE(HEADER=OFF);
```

`TO TABLE` options shown: `HEADER=ON|OFF` and `EXPORT='file.csv'`. The grammar also includes `NOHEADER` and `NO_HEADER`.

**Recipe: ordering results**
```sql
SELECT a.href
FROM doc
WHERE href IS NOT NULL
ORDER BY doc_order ASC
LIMIT 50;
```

`ORDER BY` supports multiple fields and `DESC`:
```sql
SELECT * FROM doc ORDER BY tag, node_id DESC LIMIT 20;
```

Code-accurate notes for this branch:
- Runtime sorting is implemented for: `node_id`, `tag`, `text`, `parent_id`, `max_depth`, `doc_order`.
- `sibling_pos` is accepted by validation but is not currently applied by the sorter.
- `ORDER BY` does not support arbitrary expressions or attribute keys.
- For `SUMMARIZE(*)`, only `ORDER BY tag` or `ORDER BY count` is supported.
- For other aggregates, `ORDER BY` is rejected.

## REPL workflow

**REPL workflow (step-by-step loop)**

Start REPL:

```bash
./build/markql --interactive --input ./data/index.html
```

Then use this loop:

1) Ask for help when you forget a command:
```text
.help
```

2) Load a different page (file or URL) as needed:
```text
.load ./data/index.html
```

Optional: load with an alias:
```text
.load https://example.com --alias page
```

3) Summarize what the input looks like before writing complex queries:
```text
.summarize
```

4) Start with a tiny sample:
```sql
SELECT * FROM doc LIMIT 5;
```

5) Add one filter at a time:
```sql
SELECT a FROM doc WHERE href IS NOT NULL LIMIT 20;
```

6) Project only the fields you want:
```sql
SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL LIMIT 20;
```

7) Switch output mode if you want:
```text
.mode json
```

8) Export once it looks right:
```sql
SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL TO CSV('links.csv');
```

Helpful REPL commands listed in the guide:

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

Vim navigation mode:
- Default editor mode is normal.
- Press `Esc` to switch into Vim normal mode.
- In Vim mode, press `Esc` to toggle between `vim:edit` and `vim:normal`, and back to normal mode.
- Prompts:
  - normal: `markql> `
  - vim normal: `markql (vim:normal)> `
  - vim edit: `markql (vim:edit)  > ` (padded to keep width aligned)
- Vim keys: `h/j/k/l`, `i/a/I/A`, `o/O`.

## Troubleshooting and best practices

**Error Decoder**

| Error message | What it means | How to fix |
|---|---|---|
| `Expected FROM` | Your query is missing the `FROM ...` clause. | Add `FROM doc` (or another source). Example: `SELECT a FROM doc;` |
| `Expected attributes, tag, text... after child` | The parser rejected your axis field access. | Use `child.attributes.<name>` (and similarly for `parent/ancestor/descendant`). |
| `FLATTEN_TEXT() requires AS (...)` | This message may appear in older/stale builds. In the current parser, `FLATTEN(...)`/`FLATTEN_TEXT(...)` can run without `AS` and default to one column named `flatten_text`. | If you need multiple columns or explicit names, use `AS (col1, col2, ...)`. If this error appears unexpectedly, confirm you are running the latest built binary. |
| `TEXT() must be used to project text` | You projected text as `a.text` instead of using the text function. | Use `TEXT(a)` (for example: `SELECT a.href, TEXT(a) FROM doc ...`). |
| `TEXT()/INNER_HTML() requires a non-tag filter` | You used `TEXT()` or `INNER_HTML()` without a `WHERE` predicate that is not just a tag selection. | Add a predicate like `id = '...'`, `attributes.class = '...'`, or an axis predicate. |

**If you get zero rows (not an error, but common)**

- Attribute names must match exactly (example given: `data-field` vs `data-fields`).
- Start broader first (`CONTAINS`) and then tighten.
- Keep a `LIMIT` while exploring.

**Best Practices + Performance tips**

- Start with `LIMIT 5` or `LIMIT 20`. Keep output small until you trust the filter.
- Build queries in layers:
  - First: return tags (`SELECT a FROM doc LIMIT 20;`)
  - Then: add `WHERE` (`... WHERE href IS NOT NULL`)
  - Then: project fields/functions (`SELECT a.href, TEXT(a) ...`)
  - Then: export (`TO LIST`, `TO CSV`, `TO TABLE`)
- Prefer “broader then narrower”:
  - Use `CONTAINS` to discover matching shape.
  - Switch to `=` or more specific axes once you confirm the attribute names.
- When using axes, be explicit with attribute access:
  - `descendant.attributes.data-field` (safe in this branch)
- When using `TEXT()` or `INNER_HTML()`, always include a `WHERE` predicate that targets a specific subset first.

## Copy/paste starter pack

These are 15 ready queries you can paste into the CLI or REPL and then tweak.

```sql
SELECT * FROM doc LIMIT 5;
```

```sql
SELECT div FROM doc LIMIT 10;
```

```sql
SELECT a FROM doc LIMIT 50;
```

```sql
SELECT COUNT(a) FROM doc;
```

```sql
SELECT a.href FROM doc WHERE href IS NOT NULL TO LIST();
```

```sql
SELECT a.href FROM doc WHERE href CONTAINS 'https' TO LIST();
```

```sql
SELECT a.href FROM doc WHERE href ~ '.*\.pdf$' TO LIST();
```

```sql
SELECT div FROM doc WHERE id = 'main';
```

```sql
SELECT div FROM doc WHERE attributes.class = 'summary' LIMIT 20;
```

```sql
SELECT section FROM doc WHERE attributes.class CONTAINS 'content' LIMIT 20;
```

```sql
SELECT span FROM doc WHERE parent.tag = 'div' LIMIT 50;
```

```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE ancestor.id = 'navbar' AND href IS NOT NULL
LIMIT 50;
```

```sql
SELECT TEXT(div) FROM doc WHERE attributes.class = 'summary' LIMIT 20;
```

```sql
SELECT table FROM doc TO TABLE();
```

```sql
SELECT a.href, TEXT(a)
FROM doc
WHERE href IS NOT NULL
TO CSV('links.csv');
```

## FAQ

**What is `doc`?**  
`doc` is a common alias used in examples, not a reserved source keyword.  
In this branch, any bare identifier after `FROM` (for example `FROM doc`, `FROM page`) is treated as an alias for the current in-memory document source.

**What is `document`?**  
`document` is also shown as a source form. Both `doc` and `document` appear in the provided examples and grammar diagrams.

**Can I query a local file path directly in SQL?**  
Yes. A string literal can be used as a source: `FROM 'page.html'`.

**Can I query a URL directly?**  
Yes. A string literal can be a URL source: `FROM 'https://example.com'`.

**How do I test a query quickly without scrolling forever?**  
Add `LIMIT` early: `... LIMIT 5;`. Also consider `.max_rows` in REPL for display control.

**Why does output show `data_id` instead of `data-id`?**  
MarkQL normalizes output column names by default for tool compatibility (`data-id` -> `data_id`).  
Use `.set colnames raw` to keep raw names, and `DESCRIBE LAST` to inspect the `raw_name -> output_name` mapping of the previous query.

**Why does `TEXT(div)` fail unless I add `WHERE`?**  
Because `TEXT()` (and `INNER_HTML()`) require a `WHERE` clause with a non-tag filter. Add something like `WHERE id = 'card'` or `WHERE attributes.class = 'summary'`.

**What’s the difference between `FLATTEN_TEXT` and `FLATTEN`?**  
They are aliases and behave the same.  
`FLATTEN_TEXT` is the original explicit name; `FLATTEN` is the shorter alias and is recommended as the default style in this guide.

**Why does `child.foo` fail but `child.attributes.foo` works?**  
This branch requires explicit axis attribute access using `child.attributes.<name>` (and the same pattern for `parent`, `ancestor`, `descendant`).

**How does `EXISTS(axis [WHERE ...])` work?**  
Use one of: `self`, `parent`, `child`, `ancestor`, `descendant`.  
`EXISTS(child)` checks if a node has any child.  
`EXISTS(child WHERE tag = 'h2')` checks if any child matches that predicate.  
Inner conditions are evaluated on the same axis node, so this is valid and strict:
`EXISTS(child WHERE tag = 'span' AND attributes.class = 'price')`.

**Does MarkQL support `ORDER BY` and `EXCLUDE`?**  
Yes.

`ORDER BY`:
- Supported for normal queries with fields: `node_id`, `tag`, `text`, `parent_id`, `max_depth`, `doc_order`.
- Supports multi-field ordering and `ASC`/`DESC`.
- For `SUMMARIZE(*)`, only `ORDER BY tag` or `ORDER BY count` is supported.
- Not supported with other aggregate queries.
- Note: `sibling_pos` is accepted by validation in this branch, but current sorter behavior is centered on the fields listed above.

`EXCLUDE`:
- Supported only with `SELECT *`.
- Supported fields: `node_id`, `tag`, `attributes`, `parent_id`, `max_depth`, `doc_order`, `source_uri`.
- Syntax supports a single field or a list:
  - `SELECT * EXCLUDE source_uri FROM doc;`
  - `SELECT * EXCLUDE (source_uri, tag) FROM doc;`
- It fails if all output columns are excluded.

**How do I discover what’s available in my build?**  
Use built-in self-discovery queries:
```sql
SHOW FUNCTIONS;
SHOW AXES;
SHOW OPERATORS;
DESCRIBE doc;
DESCRIBE language;
```
