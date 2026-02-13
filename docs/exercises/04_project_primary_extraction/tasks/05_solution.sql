/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         class_bucket: CASE
           WHEN attributes.data-kind = 'flight' THEN 'transport'
           ELSE 'other'
         END
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
ORDER BY node_id;
