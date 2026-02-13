/* FIXTURE: ../fixtures/page.html */
SELECT li.node_id,
       FLATTEN(li) AS (item_name, item_qty)
FROM doc
WHERE parent.attributes.class = 'line-items'
ORDER BY node_id;
