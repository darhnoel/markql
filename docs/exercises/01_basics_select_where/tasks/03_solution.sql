/* FIXTURE: ../fixtures/page.html */
SELECT section(node_id, tag)
FROM doc
WHERE tag = 'section'
  AND text LIKE '%Tokyo%'
ORDER BY node_id;
