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
./scripts/build/build.sh
```

Build note:
- `scripts/build/build.sh` now uses the repo `vcpkg.json` manifest to install FlatBuffers into `./vcpkg_installed` and to provide `flatc` during the build.
- This keeps the FlatBuffers dependency path local to the repo. System-wide `apt` packages are not required for the `DOCN` migration.
- `scripts/build/build.sh` now auto-detects a default `vcpkg` triplet for Linux, macOS, and Windows shell environments. Override with `VCPKG_TARGET_TRIPLET` / `VCPKG_HOST_TRIPLET` when cross-compiling to a non-default target.

Create a parsed document snapshot once:
```bash
./build/markql --input ./data/index.html --write-mqd ./cache/index.mqd
```

Create a prepared query once:
```bash
./build/markql --query "SELECT a.href, TEXT(a) FROM doc WHERE href IS NOT NULL" \
  --write-mqp ./cache/links.mqp
```

Inspect artifact metadata:
```bash
./build/markql --artifact-info ./cache/index.mqd
```

Run one query:
```bash
./build/markql --query "SELECT a FROM doc WHERE href CONTAINS 'https'" --input ./data/index.html
```

Run REPL:
```bash
./build/markql --interactive --input ./data/index.html
```

Lint query (parse + validate only, no execution):
```bash
./build/markql --lint "SELECT FROM doc"
```

Force colorized human diagnostics:
```bash
./build/markql --lint "SELECT FROM doc" --color=always
```

Lint as JSON:
```bash
./build/markql --lint "SELECT FROM doc" --format json
```

Render a `.mql.j2` query file into plain MarkQL and lint the rendered text:
```bash
./build/markql \
  --query-file tests/fixtures/render/generic_query.mql.j2 \
  --render j2 \
  --vars tests/fixtures/render/generic_query.toml \
  --lint
```

Write rendered MarkQL to a file before continuing through lint/execute:
```bash
./build/markql \
  --query-file tests/fixtures/render/generic_query.mql.j2 \
  --render j2 \
  --vars tests/fixtures/render/generic_query.toml \
  --rendered-out /tmp/generic_query.mql \
  --lint
```

Preview rendered MarkQL on stdout:
```bash
./build/markql \
  --query-file tests/fixtures/render/generic_query.mql.j2 \
  --render j2 \
  --vars tests/fixtures/render/generic_query.toml \
  --rendered-out -
```

Check version + provenance:
```bash
./build/markql --version
```

Compatibility note:
- `./build/markql` remains available as a legacy command name.

## Template Query Files

MarkQL supports an opt-in pre-parse render step for templated query files.

MVP contract:
- Existing `.mql` / `.sql` query-file behavior is unchanged when `--render` is omitted.
- `.mql.j2` rendering is never auto-detected; you must pass `--render j2`.
- `--vars <file.toml>` is the supported variable format in this MVP.
- Rendered output is plain MarkQL text; existing parser, lint, and execution paths run on that rendered text.
- Jinja variables use strict undefined behavior, so missing values fail before MarkQL parsing.
- `--rendered-out <file.mql>` writes the rendered MarkQL without changing the lint/execute flow.
- `--rendered-out -` is a stdout preview mode for the rendered MarkQL text.

Recommended naming:
- template: `query.mql.j2`
- vars: `query.toml`
- rendered output: `query.mql`

## Artifact Workflow

Experimental status:
- `.mqd` / `.mqp` artifact support is still experimental in this branch.
- The current files are versioned and validated, but the workflow and internal payload choices are not yet presented as a long-term frozen interface.

MarkQL can cache two conservative, versioned artifacts:

- `.mqd`: a serialized parsed `doc` snapshot
- `.mqp`: a serialized prepared query

These artifacts only remove repeated work:

- `.mqd` avoids reparsing the same HTML into the node-table form
- `.mqp` avoids reparsing and revalidating the same query text

They do not change query semantics. A direct HTML + SQL run and an equivalent `.mqd` + `.mqp` run should produce the same rows.

Recommended usage:

```bash
./build/markql --input ./data/index.html --write-mqd ./cache/index.mqd
./build/markql --query-file ./queries/links.sql --write-mqp ./cache/links.mqp
./build/markql --query-file ./cache/links.mqp --input ./cache/index.mqd
```

Compatibility rules for the MVP:

- Artifact files have explicit magic bytes and a format version.
- Readers reject incompatible artifact format major versions.
- Readers also reject artifacts produced by a different MarkQL major version.
- Readers reject unknown required feature flags.
- Readers verify a payload checksum before parsing sections.
- Unknown future sections are skipped so minor/additive format growth is possible.
- `.mqd` keeps the existing MarkQL artifact envelope, but the `DOCN` payload is now encoded with FlatBuffers.
- `.mqp` keeps the same outer artifact envelope, but the `QAST` payload is now encoded with FlatBuffers.
- Older manual-`QAST` `.mqp` files still read through a narrow legacy fallback when the FlatBuffers required-feature bit is absent.

Current limitations:

- Artifact loading is a CLI `--input` / `--query-file` feature. Query-level `FROM 'file.mqd'` is not part of this MVP.
- `--lint` only accepts SQL text, not `.mqp`.
- `.mqp` creation accepts exactly one SQL statement.
- Compression is not used in the MVP artifact format.

Security / trust model:

- Treat `.mqd` and `.mqp` as untrusted files.
- All textual artifact fields are strict UTF-8. Malformed text is rejected.
- Readers bound file bytes, section count, section size, string bytes, node count, attribute count, and collection counts before allocating.
- `.mqd` verification is two-stage:
  - MarkQL validates the outer header, section table, required features, and checksum first.
  - Then the `DOCN` payload is verified with the FlatBuffers verifier and file identifier before any fields are read.
- `.mqp` verification follows the same pattern:
  - MarkQL validates the outer header, section table, required features, and checksum first.
  - Then the `QAST` payload is verified with the FlatBuffers verifier and file identifier before any fields are read.
- `--artifact-info` escapes control characters before printing artifact-derived text such as `source_uri`.
- The checksum is a corruption/tamper detector, not an authenticity guarantee.

Prepared-query semantic boundary:
- `.mqp` persists the prepared-query meaning represented by the validated `Query` AST shape.
- It does not persist executor-private state or raw C++ memory layouts.
- The outer MarkQL envelope still owns compatibility, checksums, metadata, and required-feature gating.

Benchmark snapshot on `examples/html/koku_tk.html`:
- Method: `tests/bench_artifacts.cpp` runs 31 iterations and reports medians for query parse, query prepare, `.mqp` write, `.mqp` load, query-text execution on raw HTML, `.mqp` execution on raw HTML, `.mqp` execution on `.mqd`, and `.mqp` size.
- Current result on this fixture: `query_parse_ms_median=0.060`, `query_prepare_ms_median=0.074`, `mqp_write_ms_median=0.090`, `mqp_load_ms_median=0.146`, `query_text_on_raw_html_ms_median=9.608`, `mqp_on_raw_html_ms_median=9.642`, `mqp_on_mqd_ms_median=2.987`, `mqp_bytes=1079`.
- Exact reading: on this fixture `.mqp` load is cheap, but `.mqp` on raw HTML is effectively the same cost as query text on raw HTML because HTML parsing still dominates. The larger end-to-end win shows up when `.mqp` is paired with `.mqd`.

## Diagnostics and Lint

Use lint mode to validate syntax + key semantic rules without loading/executing data:

```bash
./build/markql --lint "SELECT TEXT(node_div) FROM doc AS node_div WHERE node_div.tag = 'div'"
```

Default lint output provides one diagnostic block per issue:
- an upfront lint summary with parse status and validation coverage
- severity and stable `code`
- diagnostic `category`
- caret-positioned snippet
- `why:` MarkQL-specific explanation
- `help:` fix guidance
- `example:` short valid replacement syntax when available

For parse failures, lint now prefers local repair guidance when it can prove the failing context.
Examples:
- typo-like operator/keyword/axis mistakes suggest the nearest supported token when the current grammar position is known
- local enum/value mistakes such as `SHOW ...`, `DESCRIBE ...`, `TO ...`, and `TABLE(...)` option values stay in that construct instead of falling back to top-level clause-order help
- function/CASE/POSITION/EXISTS parse failures show local examples instead of generic top-level query templates

Clean lint output is intentionally cautious:
- `Result: ... (no proven diagnostics)` means no implemented rule fired
- `Validation coverage: full` means the full current lint path ran
- `Validation coverage: reduced` means relation-style features (`WITH`, `JOIN`, CTE/derived sources) parsed successfully but only the reduced validation path ran
- a clean reduced result should not be read as “query is definitely correct”

Lint text color controls:
- `--color=always`: always emit ANSI colors for human lint text
- `--color=auto`: emit ANSI colors only on a TTY
- `--color=never` (or `--color=disabled`): force plain text
- `NO_COLOR` overrides color and forces plain text

`--format json` is always uncolored for automation stability.

When linting a templated query file:
- `--lint --query-file query.mql.j2 --render j2 --vars query.toml` lints the rendered MarkQL text.
- TOML/Jinja failures are reported as render/tooling failures, not as MarkQL parse diagnostics.

`--format json` now emits a top-level lint result object:
- `summary`
- `diagnostics`

`summary` includes:
- `parse_succeeded`
- `coverage`
- `relation_style_query`
- `used_reduced_validation`
- `status`
- counts for errors/warnings/notes

Each `diagnostics[]` entry keeps the existing diagnostic fields and now includes richer editor-friendly fields:
- `category`
- `why`
- `example`
- `expected`
- `encountered`

`SELECT <from_alias>` now emits a warning (`MQL-LINT-0001`) because alias-as-value
is ambiguous; use `SELECT self` for current-row node projection.

Suspicious qualified member accesses can also emit warnings when MarkQL accepts them as dynamic attribute syntax but they look like likely mistakes, for example a near-miss built-in field such as `alias.tagy`.

`doc_ref` remains available in JSON diagnostics output.

JSON format is available for automation:

```bash
./build/markql --lint "SELECT FROM doc" --format json
```

Exit codes:
- `0` no ERROR diagnostics
- `1` one or more ERROR diagnostics
- `2` CLI/tooling failure

Normal query execution uses the same diagnostic formatting for invalid queries (instead of raw exception text).

## Version and Provenance

CLI version output includes provenance:
- semantic version
- git commit hash
- `-dirty` suffix when built from a dirty worktree
- version source of truth: `python/markql/_meta.py` (`__version__`)

Python exposes the same core provenance:

```python
import markql
print(markql.__version__)
print(markql.core_version())
print(markql.core_version_info())
```

Artifact files also record producer version metadata. `--artifact-info` shows:

- artifact type (`mqd` or `mqp`)
- artifact format version
- producer version/major
- language version/major
- compatibility verdict
- document source URI + node count, or prepared query kind/source kind

## Fast Start: 5 Queries

```sql
SELECT div FROM doc LIMIT 5;
```

```sql
SELECT node_link FROM doc AS node_link WHERE node_link.href CONTAINS 'https';
```

```sql
SELECT node_link.href FROM doc AS node_link WHERE node_link.rel = 'preload' TO LIST();
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
SELECT li FROM PARSE('<ul><li>1</li><li>2</li></ul>') AS node_fragment;
```

For repeated CLI runs, you can also treat a `.mqd` snapshot as the `--input` source:

```bash
./build/markql --query "SELECT a.href FROM doc WHERE href IS NOT NULL" \
  --input ./cache/index.mqd
```

Alias sources:
```sql
SELECT node_doc FROM document AS node_doc WHERE node_doc.id = 'login';
```

## Aliases and fields

Use `alias.field` as the primary row-reference style.

`FROM doc` binds an implicit row alias named `doc`:

```sql
SELECT doc.node_id, doc.tag
FROM doc
WHERE doc.tag = 'div';
```

You can still bind an explicit alias:

```sql
SELECT node_div.node_id, node_div.tag
FROM doc AS node_div
WHERE node_div.tag = 'div';
```

Rules:
- Once you alias `doc` (for example `FROM doc AS node_div`), use only that alias in row references.
- `doc.*` is not bound in that scope.
- Recommended style (not required by the language):
  - `node_<semantic>` for DOM node rows.
  - `r_<semantic>` for CTE/derived logical rows.

`PARSE(...)` accepts either:
- an HTML string expression
- a subquery returning HTML strings (single projected column)

Example:
```sql
SELECT li
FROM PARSE(
  SELECT inner_html(div, 2)
  FROM doc
  WHERE attributes.class = 'pagination'
) AS node_fragment;
```

Compatibility note:
- `FRAGMENTS(...)` still works but is deprecated.
- Migration: `FRAGMENTS(x)` -> `PARSE(x)`.

## WITH, JOIN, and LATERAL

MarkQL supports SQL-style CTEs and joins with deterministic row order.

- `WITH ...` defines statement-local relations.
- `JOIN` / `LEFT JOIN` use `ON ...`.
- `CROSS JOIN` is cartesian and does not take `ON`.
- `CROSS JOIN LATERAL (...) AS node_right` runs the right subquery per left row (flatMap behavior).

Efficient baseline pattern (filtered rows + lateral expansion + selective left joins):

```sql
WITH r_rows AS (
  SELECT node_row.node_id AS row_id
  FROM doc AS node_row
  WHERE node_row.tag = 'tr' AND EXISTS(child WHERE tag = 'td')
),
r_cells AS (
  SELECT
    r_row.row_id,
    node_cell.sibling_pos AS pos,
    TEXT(node_cell) AS val
  FROM r_rows AS r_row
  CROSS JOIN LATERAL (
    SELECT node_cell
    FROM doc AS node_cell
    WHERE node_cell.parent_id = r_row.row_id
      AND node_cell.tag = 'td'
  ) AS node_cell
)
SELECT
  r_row.row_id,
  r_cell_2.val AS item_id,
  r_cell_4.val AS item_name
FROM r_rows AS r_row
LEFT JOIN r_cells AS r_cell_2 ON r_cell_2.row_id = r_row.row_id AND r_cell_2.pos = 2
LEFT JOIN r_cells AS r_cell_4 ON r_cell_4.row_id = r_row.row_id AND r_cell_4.pos = 4
ORDER BY r_row.row_id;
```

Derived-table source form is also supported:

```sql
SELECT r_rows.row_id
FROM (
  SELECT node_row.node_id AS row_id
  FROM doc AS node_row
  WHERE node_row.tag = 'tr'
) AS r_rows
ORDER BY r_rows.row_id;
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
- `HAS_DIRECT_TEXT` (legacy operator shorthand)
- `EXISTS(axis [WHERE expr])`

Examples:
```sql
SELECT div FROM doc WHERE id = 'main';
SELECT node_link FROM doc AS node_link WHERE node_link.href IN ('/a','/b');
SELECT node_link FROM doc AS node_link WHERE node_link.href ~ '.*\.pdf$';
SELECT div FROM doc WHERE text LIKE '%coupon%';
SELECT div FROM doc WHERE POSITION('coupon' IN LOWER(text)) > 0;
SELECT div FROM doc WHERE attributes IS NULL;
SELECT div FROM doc WHERE DIRECT_TEXT(div) LIKE '%buy now%';
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
- Use `child.attr.foo`, `parent.attr.foo`, `descendant.attr.foo`, etc.
- `attr` is a shorthand alias of `attributes` (`child.attributes.foo` still works).
- Prefer `attr` in new queries for shorter, consistent syntax.
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
- `REGEX_REPLACE(str, pattern, repl)` (ECMAScript regex)
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

```sql
SELECT REGEX_REPLACE(TRIM(TEXT(div)), '[^0-9]', '') AS digits
FROM doc
WHERE attributes.class = 'price';
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
- Prefer `DIRECT_TEXT(td) LIKE '%2025%'` as the default direct-text filter form.
- `HAS_DIRECT_TEXT` remains available as operator shorthand (`td HAS_DIRECT_TEXT '2025'`).
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
Extract HTML table rows. Default behavior is backward compatible: without new options, output matches prior rectangular table output.
```sql
SELECT table FROM doc TO TABLE();
SELECT table FROM doc TO TABLE(HEADER=OFF);
SELECT table FROM doc WHERE id = 'stats' TO TABLE(EXPORT='stats.csv');
```

Options:
- `HEADER=ON|OFF` (default `ON`)
- `NOHEADER` / `NO_HEADER` (same as `HEADER=OFF`)
- `HEADER_NORMALIZE=ON|OFF` (default `ON` when `HEADER=ON`)
- `TRIM_EMPTY_ROWS=OFF|ON` (default `OFF`)
- `TRIM_EMPTY_COLS=OFF|TRAILING|ALL` (default `OFF`)
- `EMPTY_IS=BLANK_OR_NULL|NULL_ONLY|BLANK_ONLY` (default `BLANK_OR_NULL`)
- `STOP_AFTER_EMPTY_ROWS=<int>` (default `0`, disabled)
- `FORMAT=RECT|SPARSE` (default `RECT`)
- `SPARSE_SHAPE=LONG|WIDE` (default `LONG`, only when `FORMAT=SPARSE`)
- `EXPORT='path.csv'` (single-table CSV export)

Semantics:
- `TRIM_EMPTY_ROWS=ON`: drop rows where every cell is empty under `EMPTY_IS`.
- `TRIM_EMPTY_COLS=OFF`: keep all columns.
- `TRIM_EMPTY_COLS=TRAILING`: drop only right-edge empty columns.
- `TRIM_EMPTY_COLS=ALL`: drop all columns empty across remaining rows (including internal empty columns).
- `EMPTY_IS=BLANK_OR_NULL`: empty means blank text or missing/unextractable cell.
- `EMPTY_IS=NULL_ONLY`: empty means missing/unextractable cell only.
- `EMPTY_IS=BLANK_ONLY`: empty means normalized blank text only.
- `STOP_AFTER_EMPTY_ROWS=N` (`N > 0`): stop output after `N` consecutive all-empty rows.
- `FORMAT=RECT`: rectangular rows/cells (default).
- `FORMAT=SPARSE, SPARSE_SHAPE=LONG`: emit one row per non-empty cell with `row_index`, `col_index`, optional `header` (when `HEADER=ON`), and `value`.
- `FORMAT=SPARSE, SPARSE_SHAPE=WIDE`: emit one object per table row with only non-empty keys; keys use normalized header when `HEADER=ON`, else `col_<n>`.
- `HEADER_NORMALIZE=ON`: trim/collapse whitespace, remove duplicate adjacent tokens, keep Unicode, fallback to `col_<n>` when result is empty.

Trim example:
```sql
SELECT table FROM doc
TO TABLE(TRIM_EMPTY_ROWS=ON, TRIM_EMPTY_COLS=TRAILING);
```

Sparse LONG example:
```sql
SELECT table FROM doc
TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=LONG, HEADER=ON);
```

Sparse WIDE example:
```sql
SELECT table FROM doc
TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=WIDE, HEADER=ON);
```

Determinism:
- Same DOM snapshot + same query/options => byte-identical output.

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
- `.mode duckbox|json|plain|csv`
- `.set colnames raw|normalize`
- `.lint on|off`
- `.display_mode more|less`
- `.max_rows <n|inf>`
- `DESCRIBE LAST`
- `.summarize [doc|alias|path|url]`
- `.reload_config`
- `.quit`

`csv` mode writes rectangular query results directly to stdout. It does not render `TO TABLE()` results; for extracted HTML tables, use `TO TABLE(EXPORT='file.csv')`.

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
