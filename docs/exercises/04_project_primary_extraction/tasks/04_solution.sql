/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         price_final: COALESCE(TEXT(span WHERE attributes.role = 'text'), 'N/A')
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
ORDER BY node_id;
