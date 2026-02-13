/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag, max_depth)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.data-kind = 'gold'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
ORDER BY node_id;
