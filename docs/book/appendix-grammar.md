# Appendix A: Grammar Notes

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
- `FLATTEN(tag[, depth]) AS (c1, c2, ...)`
- `PROJECT(tag) AS (alias: expr, ...)`

## Predicates
- Boolean: `AND`, `OR`, parentheses
- Comparisons: `=`, `<>`, `<`, `<=`, `>`, `>=`, `LIKE`, `IN`, `IS NULL`, `IS NOT NULL`
- Structural: `EXISTS(axis WHERE ...)`
- Axes: `parent`, `child`, `ancestor`, `descendant`

## Notes on current behavior
- `PROJECT` / `FLATTEN_EXTRACT` requires `AS (alias: expr, ...)`.
- `FLATTEN_TEXT` / `FLATTEN` uses ordered descendant text slices.
- `ORDER BY` currently supports core row fields.
