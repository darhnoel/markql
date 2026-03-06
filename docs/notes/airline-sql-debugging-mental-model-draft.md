# Draft: Beginner Walkthrough for Fixing `airline.sql`

This document explains a beginner-friendly thought process from:

- broken query
- to working query
- on `examples/html/flight_data.html`

The goal is practical debugging, not theory.

## Goal First (What we want)

Final table should have one row per flight card (`article`) with columns:

- `article_id`
- `airline_name`
- `stops`
- `duration`
- `price`
- `coverage`
- `pdf_href`
- `excel_href`

## Step 1: Run the query and trust the first error

Always start here:

```bash
./build/markql --lint --query-file airline.sql --color=disabled
```

Why:

- The first parser error is usually the real blocker.
- Do not try to “fix everything at once”.

In our case, the original query failed because:

- `self.aria-field` was not valid in that form.
- `LEFT JOIN` was placed after `WHERE` (wrong order).
- there was a trailing comma before final `SELECT`.

## Step 2: Confirm the HTML actually contains your target fields

Before writing SQL, inspect target HTML quickly:

```bash
sed -n '1,220p' examples/html/flight_data.html
```

What we confirmed:

- there are multiple `<article>` blocks
- card fields are in `div data-field="..."`
- `airline_name` is nested inside `div aria-label="..."`
- links are `<a href="...pdf">` / `<a href="...xlsx">`

Important beginner rule:

- If your query asks for `aria-field` but HTML uses `data-field`, results will be wrong even if SQL is valid.

## Step 3: Build a tiny “anchor” query

Start with the simplest reliable shape:

```sql
WITH cards AS (
  SELECT a.node_id AS article_id
  FROM doc AS a
  WHERE a.tag = 'article'
)
SELECT c.article_id
FROM cards AS c
ORDER BY article_id;
```

Why:

- If this fails, everything else will fail.
- If this works, you now have stable row IDs to join against.

## Step 4: Extract one concept per CTE

Do not build giant logic in one SELECT.

Use separate CTEs:

- `field_values`: text for `stops`, `duration`, `price`, `coverage`
- `airline_names`: nested `aria-label`
- `links`: `href` values under each `article`

Why:

- Easier to test CTE-by-CTE
- easier to debug wrong joins

## Why `doc` appears many times (ASCII view)

Many beginners ask: "Why keep joining `doc` again and again?"

Short answer:

- `doc` is the full DOM table.
- each CTE keeps only part of the data.
- if you dropped fields, you must re-read `doc` to get them.

```text
HTML
  |
  v
+--------------------------------------+
| doc                                  |
| (all nodes: node_id, parent_id, ...) |
+--------------------------------------+
  |
  | WHERE tag='article'
  v
+-------------------------+
| cards                   |
| article_id only         |
+-------------------------+

cards has only IDs, not:
- data-field
- aria-label
- href
- text

So we must read doc again:

cards + doc(parent_id=article_id)             -> field_values
cards + doc(parent_id=article_id) + doc(...)  -> airline_names
cards + doc(parent_id=article_id)             -> links

Then:

field_values + airline_names + links + cards
                 (LEFT JOIN by article_id)
                              |
                              v
                       final output row
```

Practical rule:

- `doc` = source of truth.
- CTEs = shaped snapshots.
- snapshots are smaller, so additional extraction usually means another `doc` read.

## Step 5: Pivot row-style fields into columns

`field_values` is row-style:

- (`article_id`, `field_name`, `field_value`)

Final output is column-style:

- one row with many named columns

Use filtered `LEFT JOIN`:

- `... field_name = 'stops'`
- `... field_name = 'duration'`
- `... field_name = 'price'`
- `... field_name = 'coverage'`

## Step 6: Trim values where practical

Use:

- `LTRIM(RTRIM(...))` for text and href fields

Why:

- HTML indentation adds leading/trailing spaces/newlines.

Note for beginners:

- `price` and `coverage` can still include internal line breaks because they combine multiple child nodes.
- edge trim is clean; deep internal normalization may need a separate formatting pass.

## Step 7: Validate with real execution (not only lint)

```bash
./build/markql --input examples/html/flight_data.html --query-file airline.sql --mode json --display_mode more --color=disabled
```

Check:

- no diagnostics
- 6 rows (one per article in this fixture)
- expected columns exist

## Final Debugging Mental Model (short version)

When stuck, follow this exact order:

1. Lint first.
2. Fix syntax first.
3. Inspect fixture HTML.
4. Build anchor CTE.
5. Add one extraction CTE at a time.
6. Pivot last.
7. Trim and format.
8. Run full query on real input.

This order prevents 90% of beginner confusion in MarkQL query debugging.
