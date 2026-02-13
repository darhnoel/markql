/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         summary: TEXT(p WHERE attributes.class = 'summary'),
         stop_text: TEXT(span WHERE attributes.class CONTAINS 'stops'),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
