/* FIXTURE: ../fixtures/page.html */
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         badges_pair: CONCAT(
           COALESCE(FIRST_TEXT(span WHERE parent.attributes.class = 'badges'), 'none'),
           '|',
           COALESCE(LAST_TEXT(span WHERE parent.attributes.class = 'badges'), 'none')
         )
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
