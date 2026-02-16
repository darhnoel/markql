/* FIXTURE: fixtures/page.html */
SELECT div.node_id,
       PROJECT(div) AS (
         vendor: FIRST_ATTR(img, alt WHERE attributes.src CONTAINS '/vendors/small/'),
         primary_grade: TEXT(span WHERE attributes.class CONTAINS 'ore-grade'),
         reserve_grade: TEXT(span WHERE attributes.class CONTAINS 'reserve-grade'),
         quote_text: TEXT(span WHERE attributes.class CONTAINS 'price-main'),
         quote_jpy: TRIM(REPLACE(REPLACE(REPLACE(TEXT(span WHERE attributes.class CONTAINS 'price-main'), '¥', ''), '￥', ''), ',', '')),
         quote_time: TEXT(span WHERE attributes.class CONTAINS 'quote-time')
       )
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.data-kind = 'gold'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
ORDER BY node_id;
