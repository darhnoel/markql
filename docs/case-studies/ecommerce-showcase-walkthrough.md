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

## 1) `r_cards`: define row identity

Purpose: decide what one business row is.

```sql
WITH r_cards AS (
  SELECT
    node_card.node_id AS card_id,
    node_card.sibling_pos AS card_pos,
    node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article'
    AND node_card.class = 'product-card'
)
```

Why:

- `card_id` is the join key used everywhere else.
- `card_pos` gives stable ordering.
- `sku` is directly available on the card node.

## 2) `r_titles`: extract product name

Purpose: get title text relative to each card.

```sql
r_titles AS (
  SELECT
    r_card.card_id,
    TEXT(node_title) AS product_name
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_title
    FROM doc AS node_title
    WHERE node_title.parent_id = r_card.card_id
      AND node_title.tag = 'h3'
      AND node_title.class = 'product-title'
  ) AS node_title
)
```

Why `CROSS JOIN LATERAL`:

- Evaluate child lookup per current card.
- Keep scope strict to `parent_id = r_card.card_id`.

## 3) `r_links`: two-hop extraction for URL

Purpose: get anchor `href`.

```sql
r_links AS (
  SELECT
    r_card.card_id,
    node_link.href AS product_url
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_heading
    FROM doc AS node_heading
    WHERE node_heading.parent_id = r_card.card_id
      AND node_heading.tag = 'h3'
      AND node_heading.class = 'product-title'
  ) AS node_heading
  CROSS JOIN LATERAL (
    SELECT node_link
    FROM doc AS node_link
    WHERE node_link.parent_id = node_heading.node_id
      AND node_link.tag = 'a'
      AND node_link.class = 'product-link'
  ) AS node_link
)
```

Why two hops:

- Card does not directly own the anchor.
- Explicit path is more reliable than broad descendant matching.

## 4) `r_prices`: extract current price text

Purpose: read displayed price.

```sql
r_prices AS (
  SELECT
    r_card.card_id,
    TEXT(node_price) AS price
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_price
    FROM doc AS node_price
    WHERE node_price.parent_id = r_card.card_id
      AND node_price.tag = 'span'
      AND node_price.class = 'price-current'
  ) AS node_price
)
```

Output columns: `card_id`, `price`.

## 5) `r_ratings`: extract rating summary

Purpose: read user-facing rating text.

```sql
r_ratings AS (
  SELECT
    r_card.card_id,
    TEXT(node_rating) AS rating
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_rating
    FROM doc AS node_rating
    WHERE node_rating.parent_id = r_card.card_id
      AND node_rating.tag = 'div'
      AND node_rating.class = 'product-rating'
  ) AS node_rating
)
```

Output columns: `card_id`, `rating`.

## 6) `r_badges`: normalize repeated list items

Purpose: capture many badge rows per card first.

```sql
r_badges AS (
  SELECT
    r_card.card_id,
    node_badge.sibling_pos AS badge_pos,
    TEXT(node_badge) AS badge_text
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_badges_list
    FROM doc AS node_badges_list
    WHERE node_badges_list.parent_id = r_card.card_id
      AND node_badges_list.tag = 'ul'
      AND node_badges_list.class = 'badges'
  ) AS node_badges_list
  CROSS JOIN LATERAL (
    SELECT node_badge
    FROM doc AS node_badge
    WHERE node_badge.parent_id = node_badges_list.node_id
      AND node_badge.tag = 'li'
      AND node_badge.class = 'badge'
  ) AS node_badge
)
```

Why this shape:

- Repeated items are easier to validate in long form.
- `badge_pos` lets final query pivot to fixed columns.

## 7) Final SELECT: assemble complete output row

Purpose: merge all field CTEs back to `r_cards`.

```sql
SELECT
  r_card.card_pos,
  r_card.sku,
  r_title.product_name,
  r_price.price,
  r_rating.rating,
  r_link.product_url,
  r_badge_1.badge_text AS badge_1,
  r_badge_2.badge_text AS badge_2
FROM r_cards AS r_card
LEFT JOIN r_titles AS r_title ON r_title.card_id = r_card.card_id
LEFT JOIN r_prices AS r_price ON r_price.card_id = r_card.card_id
LEFT JOIN r_ratings AS r_rating ON r_rating.card_id = r_card.card_id
LEFT JOIN r_links AS r_link ON r_link.card_id = r_card.card_id
LEFT JOIN r_badges AS r_badge_1 ON r_badge_1.card_id = r_card.card_id AND r_badge_1.badge_pos = 1
LEFT JOIN r_badges AS r_badge_2 ON r_badge_2.card_id = r_card.card_id AND r_badge_2.badge_pos = 2
ORDER BY r_card.card_pos;
```

Why `LEFT JOIN`:

- Keep product row even when one optional field is missing.
- Missing values become `NULL` instead of dropping rows.

## 8) Practical debug flow

When adapting this pattern:

1. Run `r_cards` only.
2. Add `r_titles`.
3. Add `r_prices`, then `r_ratings`.
4. Add `r_links` (nested hop).
5. Add `r_badges`.
6. Add final joins/pivot.

If something breaks, inspect the newest CTE first.

## Files

- Fixture: `docs/case-studies/fixtures/ecommerce_showcase.html`
- Query: `docs/case-studies/queries/ecommerce_showcase.sql`

## Appendix: Incremental Debug Queries

Run each query in order against:

`docs/case-studies/fixtures/ecommerce_showcase.html`

### A) Validate row anchors (`r_cards`)

```sql
WITH r_cards AS (
  SELECT
    node_card.node_id AS card_id,
    node_card.sibling_pos AS card_pos,
    node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article'
    AND node_card.class = 'product-card'
)
SELECT r_card.card_id, r_card.card_pos, r_card.sku
FROM r_cards AS r_card
ORDER BY r_card.card_pos;
```

Expected: 3 rows, one per product card.

### B) Add r_titles

```sql
WITH r_cards AS (
  SELECT node_card.node_id AS card_id, node_card.sibling_pos AS card_pos, node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article' AND node_card.class = 'product-card'
),
r_titles AS (
  SELECT r_card.card_id, TEXT(node_title) AS product_name
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_title
    FROM doc AS node_title
    WHERE node_title.parent_id = r_card.card_id
      AND node_title.tag = 'h3'
      AND node_title.class = 'product-title'
  ) AS node_title
)
SELECT r_card.card_pos, r_card.sku, r_title.product_name
FROM r_cards AS r_card
LEFT JOIN r_titles AS r_title ON r_title.card_id = r_card.card_id
ORDER BY r_card.card_pos;
```

Expected: each card has non-NULL `product_name`.

### C) Add r_links and r_prices

```sql
WITH r_cards AS (
  SELECT node_card.node_id AS card_id, node_card.sibling_pos AS card_pos, node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article' AND node_card.class = 'product-card'
),
r_links AS (
  SELECT r_card.card_id, node_link.href AS product_url
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_heading
    FROM doc AS node_heading
    WHERE node_heading.parent_id = r_card.card_id
      AND node_heading.tag = 'h3'
      AND node_heading.class = 'product-title'
  ) AS node_heading
  CROSS JOIN LATERAL (
    SELECT node_link
    FROM doc AS node_link
    WHERE node_link.parent_id = node_heading.node_id
      AND node_link.tag = 'a'
      AND node_link.class = 'product-link'
  ) AS node_link
),
r_prices AS (
  SELECT r_card.card_id, TEXT(node_price) AS price
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_price
    FROM doc AS node_price
    WHERE node_price.parent_id = r_card.card_id
      AND node_price.tag = 'span'
      AND node_price.class = 'price-current'
  ) AS node_price
)
SELECT r_card.card_pos, r_card.sku, r_price.price, r_link.product_url
FROM r_cards AS r_card
LEFT JOIN r_prices AS r_price ON r_price.card_id = r_card.card_id
LEFT JOIN r_links AS r_link ON r_link.card_id = r_card.card_id
ORDER BY r_card.card_pos;
```

Expected: all rows have `price` and `product_url`.

### D) Add r_ratings and long-form r_badges

```sql
WITH r_cards AS (
  SELECT node_card.node_id AS card_id, node_card.sibling_pos AS card_pos, node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article' AND node_card.class = 'product-card'
),
r_ratings AS (
  SELECT r_card.card_id, TEXT(node_rating) AS rating
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_rating
    FROM doc AS node_rating
    WHERE node_rating.parent_id = r_card.card_id
      AND node_rating.tag = 'div'
      AND node_rating.class = 'product-rating'
  ) AS node_rating
),
r_badges AS (
  SELECT r_card.card_id, node_badge.sibling_pos AS badge_pos, TEXT(node_badge) AS badge_text
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT node_badges_list
    FROM doc AS node_badges_list
    WHERE node_badges_list.parent_id = r_card.card_id
      AND node_badges_list.tag = 'ul'
      AND node_badges_list.class = 'badges'
  ) AS node_badges_list
  CROSS JOIN LATERAL (
    SELECT node_badge
    FROM doc AS node_badge
    WHERE node_badge.parent_id = node_badges_list.node_id
      AND node_badge.tag = 'li'
      AND node_badge.class = 'badge'
  ) AS node_badge
)
SELECT r_card.card_pos, r_card.sku, r_rating.rating, r_badge.badge_pos, r_badge.badge_text
FROM r_cards AS r_card
LEFT JOIN r_ratings AS r_rating ON r_rating.card_id = r_card.card_id
LEFT JOIN r_badges AS r_badge ON r_badge.card_id = r_card.card_id
ORDER BY r_card.card_pos, r_badge.badge_pos;
```

Expected: multiple badge rows for cards that carry multiple badges.

### E) Final pivot (badge_1 / badge_2)

Run the full query:

- `docs/case-studies/queries/ecommerce_showcase.sql`
