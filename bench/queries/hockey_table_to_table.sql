-- Reference table extraction using the documented table sink.
SELECT table
FROM doc
WHERE attributes.class = 'hockey-stats'
TO TABLE(
  HEADER=ON,
  HEADER_NORMALIZE=ON,
  TRIM_EMPTY_ROWS=ON,
  TRIM_EMPTY_COLS=TRAILING,
  EMPTY_IS=BLANK_OR_NULL,
  FORMAT=RECT
);
