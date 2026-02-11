SELECT div.node_id,
  PROJECT(div) AS (
    vendor: FIRST_ATTR(img, alt WHERE attributes.src CONTAINS '/vendors/small/'),

    primary_grade: FIRST_TEXT(div WHERE attributes.class CONTAINS 'ore-grade'),
    reserve_grade: LAST_TEXT(div WHERE attributes.class CONTAINS 'ore-grade'),

    primary_yield: FIRST_TEXT(div WHERE attributes.class CONTAINS 'yield-est'),
    reserve_yield: LAST_TEXT(div WHERE attributes.class CONTAINS 'yield-est'),

    primary_risk: FIRST_TEXT(span WHERE attributes.class CONTAINS 'risk-label'),
    reserve_risk: LAST_TEXT(span WHERE attributes.class CONTAINS 'risk-label'),

    quote_text: TEXT(span WHERE attributes.class CONTAINS 'price-main'),
    quote_jpy: TRIM(
      REPLACE(
        REPLACE(
          REPLACE(TEXT(span WHERE attributes.class CONTAINS 'price-main'), '¥', ''),
          '￥', ''
        ),
        ',', ''
      )
    )
  )
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.class CONTAINS 'offer-card'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main');
