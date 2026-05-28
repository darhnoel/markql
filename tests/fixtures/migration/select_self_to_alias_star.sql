WITH rows AS (
  SELECT node_row.node_id AS row_id
  FROM doc AS node_row
  WHERE node_row.tag = 'tr'
),
cells AS (
  SELECT self
  FROM doc AS node_cell
  WHERE node_cell.parent_id = rows.row_id
    AND node_cell.tag = 'td'
)
SELECT cell.node_id
FROM cells AS cell;
