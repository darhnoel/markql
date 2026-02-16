/* FIXTURE: ../fixtures/page.html */
SELECT div(node_id, tag)
FROM doc
WHERE tag = 'div';
