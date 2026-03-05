WITH ingredient_h2 AS (
  SELECT self
  FROM doc AS node_h2
  WHERE node_h2.tag = 'h2'
    AND TEXT(node_h2) LIKE '%គ្រឿងផ្សំ%'
  ORDER BY node_h2.node_id ASC
  LIMIT 1
),
ingredient_ul AS (
  SELECT self
  FROM doc AS node_ul
  JOIN ingredient_h2 AS h ON node_ul.node_id > h.node_id
  WHERE node_ul.tag = 'ul'
  ORDER BY node_ul.node_id ASC
  LIMIT 1
)
SELECT LTRIM(RTRIM(TEXT(node_li))) AS ingredient
FROM doc AS node_li
JOIN ingredient_ul AS u ON node_li.parent_id = u.node_id
WHERE node_li.tag = 'li'
ORDER BY node_li.node_id ASC
TO JSON();
