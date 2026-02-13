/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         first_badge: FIRST_TEXT(span WHERE parent.attributes.class = 'badges'),
         last_badge: LAST_TEXT(span WHERE parent.attributes.class = 'badges')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
