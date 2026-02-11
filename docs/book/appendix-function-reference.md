# Appendix C: Function Reference

## TL;DR
This appendix is a function checklist, not a tutorial. Pair it with chapter examples when you need behavioral context.

You can inspect the runtime list with:

```bash
./build/markql --mode plain --color=disabled --query "SHOW FUNCTIONS;"
```

## Extraction
- `TEXT(tag[, where/index])`
- `DIRECT_TEXT(tag)`
- `ATTR(tag, attr[, where/index])`
- `FIRST_TEXT(...)`, `LAST_TEXT(...)`
- `FIRST_ATTR(...)`, `LAST_ATTR(...)`
- `INNER_HTML(tag[, depth])`
- `RAW_INNER_HTML(tag[, depth])`

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
