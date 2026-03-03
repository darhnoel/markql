WITH r_cards AS (
  SELECT
    node_card.node_id AS card_id,
    node_card.sibling_pos AS card_pos,
    node_card.data_sku AS sku
  FROM doc AS node_card
  WHERE node_card.tag = 'article'
    AND node_card.class = 'product-card'
),
r_titles AS (
  SELECT
    r_card.card_id,
    TEXT(node_title) AS product_name
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_title
    WHERE node_title.parent_id = r_card.card_id
      AND node_title.tag = 'h3'
      AND node_title.class = 'product-title'
  ) AS node_title
),
r_links AS (
  SELECT
    r_card.card_id,
    node_link.href AS product_url
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_heading
    WHERE node_heading.parent_id = r_card.card_id
      AND node_heading.tag = 'h3'
      AND node_heading.class = 'product-title'
  ) AS node_heading
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_link
    WHERE node_link.parent_id = node_heading.node_id
      AND node_link.tag = 'a'
      AND node_link.class = 'product-link'
  ) AS node_link
),
r_prices AS (
  SELECT
    r_card.card_id,
    TEXT(node_price) AS price
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_price
    WHERE node_price.parent_id = r_card.card_id
      AND node_price.tag = 'span'
      AND node_price.class = 'price-current'
  ) AS node_price
),
r_ratings AS (
  SELECT
    r_card.card_id,
    TEXT(node_rating) AS rating
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_rating
    WHERE node_rating.parent_id = r_card.card_id
      AND node_rating.tag = 'div'
      AND node_rating.class = 'product-rating'
  ) AS node_rating
),
r_badges AS (
  SELECT
    r_card.card_id,
    node_badge.sibling_pos AS badge_pos,
    TEXT(node_badge) AS badge_text
  FROM r_cards AS r_card
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_badges_list
    WHERE node_badges_list.parent_id = r_card.card_id
      AND node_badges_list.tag = 'ul'
      AND node_badges_list.class = 'badges'
  ) AS node_badges_list
  CROSS JOIN LATERAL (
    SELECT self
    FROM doc AS node_badge
    WHERE node_badge.parent_id = node_badges_list.node_id
      AND node_badge.tag = 'li'
      AND node_badge.class = 'badge'
  ) AS node_badge
)
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
