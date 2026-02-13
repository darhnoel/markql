/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag, max_depth)
FROM doc
WHERE attributes.data-testid = 'offer'
ORDER BY node_id;
