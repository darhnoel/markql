# Appendix A: Grammar Notes

## TL;DR
Use this appendix as a compact syntax map while reading chapters. It summarizes forms that appear in verified examples.

This appendix summarizes practical grammar shapes used in the book.

## Core query

```sql
WITH <cte_name> AS (<select_query>) [, ...]
SELECT <projection>
FROM <source> [AS <alias>]
[<join_clause> ...]
[WHERE <predicate>]
[ORDER BY <field> [ASC|DESC]]
[LIMIT <n>]
[TO <sink>]
```

`WITH` is optional. If omitted, the query starts at `SELECT`.

## Sources
- `doc` / `document`
- `doc AS node_doc` (or `document AS node_doc`)
- `<cte_name>` / `<cte_name> AS r_rows`
- `(SELECT ...) AS r_subquery`
- `'path-or-url'`
- `'path-or-url' AS node_input`
- `RAW('<html...>')`
- `RAW('<html...>') AS node_raw`
- `PARSE('<fragment...>')`
- `PARSE(SELECT inner_html(...) FROM doc ...)`
- `PARSE(...) AS node_fragment`
- `FRAGMENTS(RAW('<fragment...>'))` (deprecated; use `PARSE(...)`)

## Joins
- `JOIN <source> AS node_right ON <expr>` (inner join)
- `LEFT JOIN <source> AS node_right ON <expr>`
- `CROSS JOIN <source> AS node_right` (no `ON`)
- `CROSS JOIN LATERAL (SELECT ...) AS node_right` (correlated per-left-row expansion)

## Projections
- Tag rows: `SELECT div FROM doc ...`
- Field projections: `SELECT a.href, a.tag FROM doc ...`
- Selector field-list projections: `SELECT a(href, tag) FROM doc ...`
- Current row node projection (canonical): `SELECT self FROM doc ...`
- Current row field projections: `SELECT self.node_id, self.tag FROM doc ...`
- `FLATTEN(tag[, depth]) AS (c1, c2, ...)`
- `PROJECT(tag) AS (alias: expr, ...)`
- Extraction forms:
  - `TEXT(tag|self)`
  - `DIRECT_TEXT(tag|self)`
  - `ATTR(tag|self, attr)`
  - `INNER_HTML(tag|self[, depth|MAX_DEPTH])`
  - `RAW_INNER_HTML(tag|self[, depth|MAX_DEPTH])`

`SELECT a(href, tag) ...` is the compact equivalent of selecting `a.href, a.tag`.

Extraction semantics (important):

- `TEXT(tag|self)` returns the node text value (aggregated text content for that node scope).
- `DIRECT_TEXT(tag|self)` returns only direct text under that node and excludes nested-element text.
- `INNER_HTML(tag|self[, depth|MAX_DEPTH])` returns minified inner HTML, with optional depth slicing.
- `RAW_INNER_HTML(tag|self[, depth|MAX_DEPTH])` returns raw (non-minified) inner HTML, with optional depth slicing.
- Only `INNER_HTML` / `RAW_INNER_HTML` use HTML depth parameters.
- In `PROJECT(...)`, `TEXT(..., n|FIRST|LAST)` and `ATTR(..., n|FIRST|LAST)` use selector position, not depth.

## Predicates
- Boolean: `AND`, `OR`, parentheses
- Comparisons: `=`, `<>`, `<`, `<=`, `>`, `>=`, `LIKE`, `IN`, `IS NULL`, `IS NOT NULL`
- Structural: `EXISTS(axis WHERE ...)`
- Axes: `parent`, `child`, `ancestor`, `descendant`
- Row-node self reference: `DIRECT_TEXT(self) LIKE '%needle%'`
- Attribute path shorthand: `attr.foo` is equivalent to `attributes.foo` (also works as `alias.attr.foo`, `parent.attr.foo`, etc.).

## Notes on current behavior
- `FROM doc` binds an implicit row alias named `doc`.
- `FROM doc AS node_doc` rebinds the row alias to `node_doc`; `doc.*` is no longer bound in that scope.
- `self` refers to the current node for the current row produced by `FROM`.
- Inside axis scopes (for example `EXISTS(descendant WHERE ...)`), `self` is rebound to the node being evaluated in that scope.
- `self` is reserved in query grammar and cannot be used as a source alias.
- `PROJECT` / `FLATTEN_EXTRACT` requires `AS (alias: expr, ...)`.
- `FLATTEN_TEXT` / `FLATTEN` uses ordered descendant text slices.
- `ORDER BY` currently supports core row fields.
- `TEXT()/INNER_HTML()/RAW_INNER_HTML()` require an outer `WHERE`.
- That `WHERE` must include a non-tag self predicate (not only `tag = ...`).
- `INNER_HTML(tag)` default depth is `1`.
- `RAW_INNER_HTML(tag)` default depth is `1`.
- `INNER_HTML(tag, MAX_DEPTH)` uses each row's `max_depth` automatically.
- `RAW_INNER_HTML(tag, MAX_DEPTH)` uses each row's `max_depth` automatically.
- In one `SELECT`, `INNER_HTML`/`RAW_INNER_HTML` depth mode must be consistent.

## Alias naming conventions (recommended)

This is a style recommendation, not a language rule.

Why this helps:
- CTE-heavy queries and joins are easier to read when alias roles are obvious.
- It reduces ambiguity between DOM node rows and logical table rows.

Rules of thumb:
- Use `node_<semantic>` for DOM node aliases (`FROM doc AS node_card`, `... AS node_link`).
- Use `r_<semantic>` for CTE/derived-table row aliases (`WITH r_rows AS (...)`, `FROM r_rows AS r_row`).

Before:

```sql
WITH rows AS (
  SELECT n.node_id AS row_id
  FROM doc AS n
  WHERE n.tag = 'tr'
),
cells AS (
  SELECT
    r.row_id,
    c.sibling_pos AS pos,
    TEXT(c) AS val
  FROM rows AS r
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS c
    WHERE c.parent_id = r.row_id
      AND c.tag = 'td'
  ) AS c
)
SELECT r.row_id, c.val
FROM rows AS r
JOIN cells AS c ON c.row_id = r.row_id;
```

After (recommended style):

```sql
WITH r_rows AS (
  SELECT node_row.node_id AS row_id
  FROM doc AS node_row
  WHERE node_row.tag = 'tr'
),
r_cells AS (
  SELECT
    r_row.row_id,
    node_cell.sibling_pos AS pos,
    TEXT(node_cell) AS val
  FROM r_rows AS r_row
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_cell
    WHERE node_cell.parent_id = r_row.row_id
      AND node_cell.tag = 'td'
  ) AS node_cell
)
SELECT r_row.row_id, r_cell.val
FROM r_rows AS r_row
JOIN r_cells AS r_cell ON r_cell.row_id = r_row.row_id;
```

## SELECT self for current row nodes

Canonical rule:
- In node-stream queries, write `SELECT self` to return the current row node.

Why:
- It reads as projection of a value expression, not as "select the row variable name".
- It reduces ambiguity in `CROSS JOIN LATERAL` workflows where aliases are used for scoping.

Compatibility:
- Legacy `SELECT <from_alias>` is still accepted for backward compatibility.
- Behavior of legacy form is preserved as-is (including query-shape differences in older flows).
- Lint warns on that ambiguous form with:
  - code: `MQL-LINT-0001`
  - message: `Selecting the FROM alias as a value is ambiguous`
  - help: `Use SELECT self to return the current node`

Migration:
- Replace `SELECT <from_alias>` with `SELECT self` when the intent is "return current row node".

## Diagnostics Quick Use

Validate grammar + semantic shape without execution:

```bash
./build/markql --lint "SELECT FROM doc"
./build/markql --lint "SELECT FROM doc" --format json
```

Color controls for human lint text:
- `--color=always` (always ANSI),
- `--color=auto` (TTY-only ANSI),
- `--color=never` / `--color=disabled` (plain text).
- `NO_COLOR` always forces plain text.
- `--format json` remains ANSI-free.

Diagnostic references:
- syntax and clause-order issues point here: `docs/book/appendix-grammar.md`
- function/projection constraints point to: `docs/book/appendix-function-reference.md`
- sink/output usage points to: `docs/markql-cli-guide.md`
