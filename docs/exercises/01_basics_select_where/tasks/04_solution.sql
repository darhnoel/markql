/* FIXTURE: ../fixtures/page.html */
SELECT section(node_id, tag)
FROM doc
WHERE tag = 'section'
ORDER BY node_id
LIMIT 1;
