/* FIXTURE: ../fixtures/page.html */
SELECT section(node_id, tag)
FROM doc
WHERE tag = 'section'
  AND EXISTS(child WHERE tag = 'span' AND attributes.class = 'price')
ORDER BY node_id;
