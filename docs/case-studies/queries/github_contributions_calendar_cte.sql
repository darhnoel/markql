WITH r_cells AS (
  SELECT
    node_cell.node_id AS node_id,
    node_cell.data-ix AS data_ix,
    node_cell.data-date AS data_date,
    node_cell.data-level AS data_level,
    LTRIM(RTRIM(node_cell.parent.text)) AS raw_day
  FROM doc AS node_cell
  WHERE node_cell.tag = 'td'
    AND node_cell.id CONTAINS 'contribution-day-component'
)
SELECT
  r_cell.data_ix,
  r_cell.data_date,
  r_cell.data_level,
  CASE
    WHEN r_cell.raw_day LIKE 'Sunday%' THEN 'Sunday'
    WHEN r_cell.raw_day LIKE 'Monday%' THEN 'Monday'
    WHEN r_cell.raw_day LIKE 'Tuesday%' THEN 'Tuesday'
    WHEN r_cell.raw_day LIKE 'Wednesday%' THEN 'Wednesday'
    WHEN r_cell.raw_day LIKE 'Thursday%' THEN 'Thursday'
    WHEN r_cell.raw_day LIKE 'Friday%' THEN 'Friday'
    WHEN r_cell.raw_day LIKE 'Saturday%' THEN 'Saturday'
    ELSE r_cell.raw_day
  END AS day
FROM r_cells AS r_cell
ORDER BY r_cell.node_id;
