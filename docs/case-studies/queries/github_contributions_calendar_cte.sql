WITH cells AS (
  SELECT
    n.node_id AS node_id,
    n.data-ix AS data_ix,
    n.data-date AS data_date,
    n.data-level AS data_level,
    LTRIM(RTRIM(n.parent.text)) AS raw_day
  FROM doc AS n
  WHERE n.tag = 'td'
    AND n.id CONTAINS 'contribution-day-component'
)
SELECT
  c.data_ix,
  c.data_date,
  c.data_level,
  CASE
    WHEN c.raw_day LIKE 'Sunday%' THEN 'Sunday'
    WHEN c.raw_day LIKE 'Monday%' THEN 'Monday'
    WHEN c.raw_day LIKE 'Tuesday%' THEN 'Tuesday'
    WHEN c.raw_day LIKE 'Wednesday%' THEN 'Wednesday'
    WHEN c.raw_day LIKE 'Thursday%' THEN 'Thursday'
    WHEN c.raw_day LIKE 'Friday%' THEN 'Friday'
    WHEN c.raw_day LIKE 'Saturday%' THEN 'Saturday'
    ELSE c.raw_day
  END AS day
FROM cells AS c
ORDER BY c.node_id;
