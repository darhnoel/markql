/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.data-kind = 'gold'
ORDER BY node_id;
