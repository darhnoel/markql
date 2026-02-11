SELECT PROJECT(td) AS (
  data_ix: ATTR(td, data-ix),
  data_date: ATTR(td, data-date),
  data_level: ATTR(td, data-level),
  day: CASE
    WHEN TRIM(parent.text) LIKE 'Sunday%' THEN 'Sunday'
    WHEN TRIM(parent.text) LIKE 'Monday%' THEN 'Monday'
    WHEN TRIM(parent.text) LIKE 'Tuesday%' THEN 'Tuesday'
    WHEN TRIM(parent.text) LIKE 'Wednesday%' THEN 'Wednesday'
    WHEN TRIM(parent.text) LIKE 'Thursday%' THEN 'Thursday'
    WHEN TRIM(parent.text) LIKE 'Friday%' THEN 'Friday'
    WHEN TRIM(parent.text) LIKE 'Saturday%' THEN 'Saturday'
    ELSE TRIM(parent.text)
  END
)
FROM doc
WHERE tag = 'td'
  AND id CONTAINS 'contribution-day-component';
