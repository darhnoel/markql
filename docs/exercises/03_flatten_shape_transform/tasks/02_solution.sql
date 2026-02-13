/* FIXTURE: ../fixtures/page.html */
SELECT ul.node_id,
       FLATTEN(ul) AS (chunk1, chunk2, chunk3, chunk4)
FROM doc
WHERE attributes.class = 'line-items'
ORDER BY node_id;
