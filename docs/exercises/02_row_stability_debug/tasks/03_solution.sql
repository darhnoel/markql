/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
ORDER BY node_id;
