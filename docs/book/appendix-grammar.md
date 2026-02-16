# Appendix A: Grammar Notes

## TL;DR
Use this appendix as a compact syntax map while reading chapters. It summarizes forms that appear in verified examples.

This appendix summarizes practical grammar shapes used in the book.

## Core query

```sql
SELECT <projection>
FROM <source>
[WHERE <predicate>]
[ORDER BY <field> [ASC|DESC]]
[LIMIT <n>]
[TO <sink>]
```

## Sources
- `doc` / `document`
- `'path-or-url'`
- `RAW('<html...>')`
- `FRAGMENTS(RAW('<fragment...>'))`

## Projections
- Tag rows: `SELECT div FROM doc ...`
- Field projections: `SELECT a.href, a.tag FROM doc ...`
- Selector field-list projections: `SELECT a(href, tag) FROM doc ...`
- Current row projections: `SELECT self.node_id, self.tag FROM doc ...`
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

## Notes on current behavior
- `self` refers to the current node for the current row produced by `FROM`.
- Inside axis scopes (for example `EXISTS(descendant WHERE ...)`), `self` is rebound to the node being evaluated in that scope.
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
