-- size-profile schema: team,w,l,ot,pts
SELECT
PROJECT(tr) AS (
  team: COALESCE(
    TEXT(a WHERE parent.attributes.class = 'team'),
    CONCAT(
      TEXT(span WHERE attributes.class = 'city'),
      TEXT(span WHERE attributes.class = 'nick')
    ),
    TRIM(DIRECT_TEXT(td WHERE attributes.class = 'team')),
    TRIM(TEXT(td WHERE attributes.class = 'team'))
  ),
  w: COALESCE(TRIM(TEXT(td WHERE attributes.class = 'w')), ''),
  l: COALESCE(TRIM(TEXT(td WHERE attributes.class = 'l')), ''),
  ot: COALESCE(TRIM(TEXT(td WHERE attributes.class = 'ot')), ''),
  pts: COALESCE(TRIM(TEXT(td WHERE attributes.class = 'pts')), '')
)
FROM doc AS tr
WHERE tr.tag = 'tr'
  AND EXISTS(child WHERE tag = 'td')
  AND (tr.attributes.class IS NULL OR tr.attributes.class <> 'totals')
ORDER BY node_id
TO CSV('/tmp/markql_hockey.csv');
