-- size-profile schema: team,w,l,ot,pts
WITH row_nodes AS (
  SELECT tr.node_id AS row_id
  FROM doc AS tr
  WHERE tr.tag = 'tr'
    AND tr.parent.tag <> 'thead'
    AND (tr.class IS NULL OR tr.class <> 'totals')
),
cell_nodes AS (
  SELECT
    c.parent_id AS row_id,
    c.node_id AS cell_id,
    c.class AS cls,
    TEXT(c) AS text_value
  FROM doc AS c
  WHERE c.tag = 'td'
    AND (
      c.class = 'team'
      OR c.class = 'w'
      OR c.class = 'l'
      OR c.class = 'ot'
      OR c.class = 'pts'
    )
),
team_anchor AS (
  SELECT
    a.parent_id AS cell_id,
    TEXT(a) AS team_value
  FROM doc AS a
  WHERE a.tag = 'a'
),
team_small_note AS (
  SELECT
    s.parent_id AS cell_id,
    TEXT(s) AS note_value
  FROM doc AS s
  WHERE s.tag = 'small'
),
row_values AS (
  SELECT
    r.row_id,
    team_anchor.team_value,
    team_cell.text_value AS team_text_value,
    REPLACE(team_cell.text_value, team_small_note.note_value, '') AS team_text_no_small,
    w_cell.text_value AS w_value,
    l_cell.text_value AS l_value,
    ot_cell.text_value AS ot_value,
    pts_cell.text_value AS pts_value
  FROM row_nodes AS r
  LEFT JOIN cell_nodes AS team_cell ON team_cell.row_id = r.row_id AND team_cell.cls = 'team'
  LEFT JOIN team_anchor ON team_anchor.cell_id = team_cell.cell_id
  LEFT JOIN team_small_note ON team_small_note.cell_id = team_cell.cell_id
  LEFT JOIN cell_nodes AS w_cell ON w_cell.row_id = r.row_id AND w_cell.cls = 'w'
  LEFT JOIN cell_nodes AS l_cell ON l_cell.row_id = r.row_id AND l_cell.cls = 'l'
  LEFT JOIN cell_nodes AS ot_cell ON ot_cell.row_id = r.row_id AND ot_cell.cls = 'ot'
  LEFT JOIN cell_nodes AS pts_cell ON pts_cell.row_id = r.row_id AND pts_cell.cls = 'pts'
)
SELECT
  COALESCE(team_value, TRIM(team_text_no_small), TRIM(team_text_value), '') AS team,
  COALESCE(w_value, '') AS w,
  COALESCE(l_value, '') AS l,
  COALESCE(ot_value, '') AS ot,
  COALESCE(pts_value, '') AS pts
FROM row_values
ORDER BY row_id
TO CSV('/tmp/markql_hockey.csv');
