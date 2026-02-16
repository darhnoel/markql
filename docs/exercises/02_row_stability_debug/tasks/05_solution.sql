/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND LOWER(text) LIKE '%terra prospect%'
ORDER BY node_id;
