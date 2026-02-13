/* FIXTURE: ../fixtures/page.html */
SELECT div.node_id,
       FLATTEN(div) AS (chunk1, chunk2, chunk3, chunk4, chunk5)
FROM doc
WHERE attributes.class = 'shipment'
ORDER BY node_id;
