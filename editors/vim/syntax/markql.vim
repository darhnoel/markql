if exists("b:current_syntax")
  finish
endif

syn case ignore

syn keyword markqlClause WITH SELECT EXCLUDE FROM JOIN LEFT INNER CROSS LATERAL ON WHERE ORDER BY LIMIT TO AS
syn keyword markqlMeta SHOW DESCRIBE DOCUMENT DOC LANGUAGE INPUT INPUTS FUNCTIONS AXES OPERATORS
syn keyword markqlFunction PROJECT FLATTEN_EXTRACT FLATTEN_TEXT FLATTEN TEXT DIRECT_TEXT FIRST_TEXT LAST_TEXT
syn keyword markqlFunction ATTR FIRST_ATTR LAST_ATTR INNER_HTML RAW_INNER_HTML RAW PARSE FRAGMENTS
syn keyword markqlFunction COUNT SUMMARIZE TFIDF COALESCE CASE WHEN THEN ELSE END POSITION LOCATE CONCAT
syn keyword markqlFunction SUBSTRING SUBSTR LENGTH CHAR_LENGTH REPLACE LOWER UPPER TRIM LTRIM RTRIM
syn keyword markqlAxis self parent child ancestor descendant
syn keyword markqlConstant MAX_DEPTH FIRST LAST ALL ANY ASC DESC LIST TABLE CSV PARQUET JSON NDJSON
syn keyword markqlConstant HEADER NOHEADER NO_HEADER EXPORT TRIM_EMPTY_ROWS TRIM_EMPTY_COLS EMPTY_IS
syn keyword markqlConstant STOP_AFTER_EMPTY_ROWS FORMAT SPARSE_SHAPE HEADER_NORMALIZE TRAILING
syn keyword markqlConstant BLANK_OR_NULL NULL_ONLY BLANK_ONLY RECT SPARSE LONG WIDE
syn keyword markqlConstant TOP_TERMS MIN_DF MAX_DF STOPWORDS NONE OFF ON ENGLISH DEFAULT

syn match markqlComment "--.*$"
syn region markqlBlockComment start="/\*" end="\*/"
syn region markqlString start=+'+ skip=+\\\\\|\\'+ end=+'+
syn region markqlString start=+"+ skip=+\\\\\|\\"+ end=+"+
syn match markqlNumber "\<\d\+\>"
syn match markqlOperator "\<IS\s\+NOT\s\+NULL\>"
syn match markqlOperator "\<IS\s\+NULL\>"
syn match markqlOperator "\<CONTAINS\s\+ALL\>"
syn match markqlOperator "\<CONTAINS\s\+ANY\>"
syn match markqlOperator "\<AND\|OR\|IN\|LIKE\|CONTAINS\|EXISTS\|NOT\|NULL\|HAS_DIRECT_TEXT\|IS\>"
syn match markqlOperatorSymbol "<>\|!=\|<=\|>=\|=\|<\|>\|[~]"
syn match markqlIdentifier "\<[A-Za-z_][A-Za-z0-9_-]*\(\.[A-Za-z_][A-Za-z0-9_-]*\)*\>"

hi def link markqlClause Keyword
hi def link markqlMeta Keyword
hi def link markqlFunction Function
hi def link markqlAxis Identifier
hi def link markqlOperator Operator
hi def link markqlOperatorSymbol Operator
hi def link markqlConstant Constant
hi def link markqlComment Comment
hi def link markqlBlockComment Comment
hi def link markqlString String
hi def link markqlNumber Number
hi def link markqlIdentifier Identifier

let b:current_syntax = "markql"
