/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
  AND EXISTS(descendant WHERE attributes.role = 'text')
ORDER BY node_id;
