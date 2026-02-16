/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         stop_text: TEXT(span WHERE attributes.class CONTAINS 'stops'),
         price_num: TRIM(REPLACE(REPLACE(TEXT(span WHERE attributes.role = 'text'), '¥', ''), ',', ''))
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
