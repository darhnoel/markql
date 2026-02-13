/* FIXTURE: ../fixtures/page.html */
SELECT section(node_id, tag)
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
