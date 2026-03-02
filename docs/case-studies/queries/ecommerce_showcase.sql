WITH cards AS (
  SELECT
    p.node_id AS card_id,
    p.sibling_pos AS card_pos,
    p.data_sku AS sku
  FROM doc AS p
  WHERE p.tag = 'article'
    AND p.class = 'product-card'
),
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
),
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
),
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
),
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
),
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
