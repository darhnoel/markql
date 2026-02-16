# Appendix C: Function Reference

## TL;DR
This appendix is a function checklist, not a tutorial. Pair it with chapter examples when you need behavioral context.

You can inspect the runtime list with:

```bash
./build/markql --mode plain --color=disabled --query "SHOW FUNCTIONS;"
```

## Extraction
- `TEXT(tag|self[, where/index])`
- `DIRECT_TEXT(tag|self)`
- `ATTR(tag|self, attr[, where/index])`
- `FIRST_TEXT(...)`, `LAST_TEXT(...)`
- `FIRST_ATTR(...)`, `LAST_ATTR(...)`
- `INNER_HTML(tag|self[, depth|MAX_DEPTH])`
- `RAW_INNER_HTML(tag|self[, depth|MAX_DEPTH])`

## Schema construction
- `FLATTEN_TEXT(tag[, depth])`
- `FLATTEN(tag[, depth])` (alias)
- `PROJECT(tag) AS (alias: expr, ...)`
- `FLATTEN_EXTRACT(tag)` (compat alias of `PROJECT`)

## Expression helpers
- `COALESCE(a, b, ...)`
- `CASE WHEN ... THEN ... [ELSE ...] END`
- `TRIM`, `LTRIM`, `RTRIM`
- `LOWER`, `UPPER`
- `REPLACE`
- `CONCAT`
- `SUBSTRING` / `SUBSTR`
- `LENGTH` / `CHAR_LENGTH`
- `POSITION(substr IN str)` / `LOCATE`

## Aggregation and analytics
- `COUNT(tag|*)`
- `SUMMARIZE(*)`
- `TFIDF(...)`

## Behavior and constraints
- `self` refers to the current node for the current row produced by `FROM`.
- Inside axis scopes such as `EXISTS(descendant WHERE ...)`, `self` is rebound to the node being evaluated in that scope.
- `TEXT()/INNER_HTML()/RAW_INNER_HTML()` require an outer `WHERE` clause.
- The outer `WHERE` must include a non-tag self predicate (for example `attributes.*`, `parent.*`, etc.), not only `tag = ...`.
- `INNER_HTML(tag)` and `RAW_INNER_HTML(tag)` default to depth `1` when depth is omitted.
- `INNER_HTML(tag, MAX_DEPTH)` and `RAW_INNER_HTML(tag, MAX_DEPTH)` auto-expand to each row's `max_depth`.
- In one `SELECT`, `INNER_HTML`/`RAW_INNER_HTML` depth mode must be consistent (do not mix default, numeric depth, and `MAX_DEPTH`).
- In one `SELECT`, do not mix `INNER_HTML()` and `RAW_INNER_HTML()` projections.
