/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-kind = 'gold'
  AND attributes.data-testid = 'offer'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
ORDER BY node_id;
