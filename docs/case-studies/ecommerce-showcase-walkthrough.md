# E-commerce Showcase Walkthrough

This page explains `queries/ecommerce_showcase.sql` CTE by CTE.

Target output: one row per product card with:

- `card_pos`
- `sku`
- `product_name`
- `price`
- `rating`
- `product_url`
- `badge_1`, `badge_2`

## 1) `cards`: define row identity

Purpose: decide what one business row is.

```sql
WITH cards AS (
  SELECT
    p.node_id AS card_id,
    p.sibling_pos AS card_pos,
    p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article'
    AND p.class = 'product-card'
)
```

Why:

- `card_id` is the join key used everywhere else.
- `card_pos` gives stable ordering.
- `sku` is directly available on the card node.

## 2) `titles`: extract product name

Purpose: get title text relative to each card.

```sql
titles AS (
  SELECT
    c.card_id,
    TEXT(t) AS product_name
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT t
    FROM doc AS t
    WHERE t.parent_id = c.card_id
      AND t.tag = 'h3'
      AND t.class = 'product-title'
  ) AS t
)
```

Why `CROSS JOIN LATERAL`:

- Evaluate child lookup per current card.
- Keep scope strict to `parent_id = c.card_id`.

## 3) `links`: two-hop extraction for URL

Purpose: get anchor `href`.

```sql
links AS (
  SELECT
    c.card_id,
    a.href AS product_url
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT h
    FROM doc AS h
    WHERE h.parent_id = c.card_id
      AND h.tag = 'h3'
      AND h.class = 'product-title'
  ) AS h
  CROSS JOIN LATERAL (
    SELECT a
    FROM doc AS a
    WHERE a.parent_id = h.node_id
      AND a.tag = 'a'
      AND a.class = 'product-link'
  ) AS a
)
```

Why two hops:

- Card does not directly own the anchor.
- Explicit path is more reliable than broad descendant matching.

## 4) `prices`: extract current price text

Purpose: read displayed price.

```sql
prices AS (
  SELECT
    c.card_id,
    TEXT(s) AS price
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT s
    FROM doc AS s
    WHERE s.parent_id = c.card_id
      AND s.tag = 'span'
      AND s.class = 'price-current'
  ) AS s
)
```

Output columns: `card_id`, `price`.

## 5) `ratings`: extract rating summary

Purpose: read user-facing rating text.

```sql
ratings AS (
  SELECT
    c.card_id,
    TEXT(r) AS rating
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT r
    FROM doc AS r
    WHERE r.parent_id = c.card_id
      AND r.tag = 'div'
      AND r.class = 'product-rating'
  ) AS r
)
```

Output columns: `card_id`, `rating`.

## 6) `badges`: normalize repeated list items

Purpose: capture many badges per card as rows first.

```sql
badges AS (
  SELECT
    c.card_id,
    b.sibling_pos AS badge_pos,
    TEXT(b) AS badge_text
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT u
    FROM doc AS u
    WHERE u.parent_id = c.card_id
      AND u.tag = 'ul'
      AND u.class = 'badges'
  ) AS u
  CROSS JOIN LATERAL (
    SELECT b
    FROM doc AS b
    WHERE b.parent_id = u.node_id
      AND b.tag = 'li'
      AND b.class = 'badge'
  ) AS b
)
```

Why this shape:

- Repeated items are easier to validate in long form.
- `badge_pos` lets final query pivot to fixed columns.

## 7) Final SELECT: assemble complete output row

Purpose: merge all field CTEs back to `cards`.

```sql
SELECT
  c.card_pos,
  c.sku,
  t.product_name,
  p.price,
  r.rating,
  l.product_url,
  b1.badge_text AS badge_1,
  b2.badge_text AS badge_2
FROM cards AS c
LEFT JOIN titles AS t ON t.card_id = c.card_id
LEFT JOIN prices AS p ON p.card_id = c.card_id
LEFT JOIN ratings AS r ON r.card_id = c.card_id
LEFT JOIN links AS l ON l.card_id = c.card_id
LEFT JOIN badges AS b1 ON b1.card_id = c.card_id AND b1.badge_pos = 1
LEFT JOIN badges AS b2 ON b2.card_id = c.card_id AND b2.badge_pos = 2
ORDER BY c.card_pos;
```

Why `LEFT JOIN`:

- Keep product row even when one optional field is missing.
- Missing values become `NULL` instead of dropping rows.

## 8) Practical debug flow

When adapting this pattern:

1. Run `cards` only.
2. Add `titles`.
3. Add `prices`, then `ratings`.
4. Add `links` (nested hop).
5. Add `badges`.
6. Add final joins/pivot.

If something breaks, inspect the newest CTE first.

## Files

- Fixture: `docs/case-studies/fixtures/ecommerce_showcase.html`
- Query: `docs/case-studies/queries/ecommerce_showcase.sql`

## Appendix: Incremental Debug Queries

Run each query in order against:

`docs/case-studies/fixtures/ecommerce_showcase.html`

### A) Validate row anchors (`cards`)

```sql
WITH cards AS (
  SELECT
    p.node_id AS card_id,
    p.sibling_pos AS card_pos,
    p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article'
    AND p.class = 'product-card'
)
SELECT c.card_id, c.card_pos, c.sku
FROM cards AS c
ORDER BY c.card_pos;
```

Expected: 3 rows, one per product card.

### B) Add titles

```sql
WITH cards AS (
  SELECT p.node_id AS card_id, p.sibling_pos AS card_pos, p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article' AND p.class = 'product-card'
),
titles AS (
  SELECT c.card_id, TEXT(t) AS product_name
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT t
    FROM doc AS t
    WHERE t.parent_id = c.card_id
      AND t.tag = 'h3'
      AND t.class = 'product-title'
  ) AS t
)
SELECT c.card_pos, c.sku, t.product_name
FROM cards AS c
LEFT JOIN titles AS t ON t.card_id = c.card_id
ORDER BY c.card_pos;
```

Expected: each card has non-NULL `product_name`.

### C) Add links and prices

```sql
WITH cards AS (
  SELECT p.node_id AS card_id, p.sibling_pos AS card_pos, p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article' AND p.class = 'product-card'
),
links AS (
  SELECT c.card_id, a.href AS product_url
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT h
    FROM doc AS h
    WHERE h.parent_id = c.card_id
      AND h.tag = 'h3'
      AND h.class = 'product-title'
  ) AS h
  CROSS JOIN LATERAL (
    SELECT a
    FROM doc AS a
    WHERE a.parent_id = h.node_id
      AND a.tag = 'a'
      AND a.class = 'product-link'
  ) AS a
),
prices AS (
  SELECT c.card_id, TEXT(s) AS price
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT s
    FROM doc AS s
    WHERE s.parent_id = c.card_id
      AND s.tag = 'span'
      AND s.class = 'price-current'
  ) AS s
)
SELECT c.card_pos, c.sku, p.price, l.product_url
FROM cards AS c
LEFT JOIN prices AS p ON p.card_id = c.card_id
LEFT JOIN links AS l ON l.card_id = c.card_id
ORDER BY c.card_pos;
```

Expected: all rows have `price` and `product_url`.

### D) Add ratings and long-form badges

```sql
WITH cards AS (
  SELECT p.node_id AS card_id, p.sibling_pos AS card_pos, p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article' AND p.class = 'product-card'
),
ratings AS (
  SELECT c.card_id, TEXT(r) AS rating
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT r
    FROM doc AS r
    WHERE r.parent_id = c.card_id
      AND r.tag = 'div'
      AND r.class = 'product-rating'
  ) AS r
),
badges AS (
  SELECT c.card_id, b.sibling_pos AS badge_pos, TEXT(b) AS badge_text
  FROM cards AS c
  CROSS JOIN LATERAL (
    SELECT u
    FROM doc AS u
    WHERE u.parent_id = c.card_id
      AND u.tag = 'ul'
      AND u.class = 'badges'
  ) AS u
  CROSS JOIN LATERAL (
    SELECT b
    FROM doc AS b
    WHERE b.parent_id = u.node_id
      AND b.tag = 'li'
      AND b.class = 'badge'
  ) AS b
)
SELECT c.card_pos, c.sku, r.rating, b.badge_pos, b.badge_text
FROM cards AS c
LEFT JOIN ratings AS r ON r.card_id = c.card_id
LEFT JOIN badges AS b ON b.card_id = c.card_id
ORDER BY c.card_pos, b.badge_pos;
```

Expected: multiple badge rows for cards with multiple badges.

### E) Final pivot (badge_1 / badge_2)

Run the full query:

- `docs/case-studies/queries/ecommerce_showcase.sql`
