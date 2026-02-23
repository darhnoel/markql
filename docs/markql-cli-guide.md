# MarkQL CLI Guide

## TO TABLE() Option Reference

`TO TABLE()` supports explicit table-shaping options.

### Header options
- `HEADER=ON|OFF` (default `ON`)
- `NOHEADER` / `NO_HEADER` (same as `HEADER=OFF`)
- `HEADER_NORMALIZE=ON|OFF` (default `OFF` unless explicitly set)
- `EXPORT='path.csv'` (CSV export for a single selected table)

### Trimming options
- `TRIM_EMPTY_ROWS=OFF|ON` (default `OFF`)
- `TRIM_EMPTY_COLS=OFF|TRAILING|ALL` (default `OFF`)
- `EMPTY_IS=BLANK_OR_NULL|NULL_ONLY|BLANK_ONLY` (default `BLANK_OR_NULL`)
- `STOP_AFTER_EMPTY_ROWS=<int>` (default `0`, disabled)

`EMPTY_IS` meanings:
- `BLANK_OR_NULL`: empty if blank text or missing cell
- `NULL_ONLY`: empty only when missing
- `BLANK_ONLY`: empty only when blank text

### Sparse options
- `FORMAT=RECT|SPARSE` (default `RECT`)
- `SPARSE_SHAPE=LONG|WIDE` (default `LONG`, used when `FORMAT=SPARSE`)

## Minimal Examples

Trim rows + trailing columns:

```sql
SELECT table FROM doc
TO TABLE(TRIM_EMPTY_ROWS=ON, TRIM_EMPTY_COLS=TRAILING);
```

Sparse LONG:

```sql
SELECT table FROM doc
TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=LONG, HEADER=ON);
```

Sparse WIDE:

```sql
SELECT table FROM doc
TO TABLE(FORMAT=SPARSE, SPARSE_SHAPE=WIDE, HEADER=ON);
```

Header normalization:

```sql
SELECT table FROM doc
TO TABLE(HEADER=ON, HEADER_NORMALIZE=ON);
```
