parser grammar MarkQLParser;

// NOTE:
// - This grammar is a parser-level baseline for MarkQL syntax.
// - A few constraints in the C++ implementation are validator/runtime checks
//   (for example: SELECT shape compatibility, TFIDF option semantics, and some
//   function guardrails) and are intentionally not encoded as pure grammar.
options {
  tokenVocab = MarkQLLexer;
}

query
    : queryBody SEMICOLON? EOF
    ;

subqueryBody
    : queryBody SEMICOLON?
    ;

queryBody
    : selectQuery
    | showQuery
    | describeQuery
    ;

showQuery
    : SHOW (INPUT | INPUTS | FUNCTIONS | AXES | OPERATORS)
    ;

describeQuery
    : DESCRIBE (DOCUMENT | DOC | LANGUAGE)
    ;

selectQuery
    : SELECT selectList
      (EXCLUDE excludeList)?
      FROM source
      (WHERE expr)?
      (ORDER BY orderByItem (COMMA orderByItem)*)?
      (LIMIT UINT)?
      (TO toTarget)?
    ;

selectList
    : selectItem (COMMA selectItem)*
    ;

selectItem
    : projectSelectItem
    | flattenSelectItem
    | selfProjectionSelectItem
    | trimSelectItem
    | countSelectItem
    | summarizeSelectItem
    | tfidfSelectItem
    | caseProjectionSelectItem
    | firstLastProjectionSelectItem
    | scalarFunctionProjectionSelectItem
    | textProjectionSelectItem
    | innerHtmlProjectionSelectItem
    | STAR
    | tagFieldListSelectItem
    | tagFieldSelectItem
    | tagOnlySelectItem
    ;

projectSelectItem
    : (PROJECT | FLATTEN_EXTRACT) LPAREN tagIdentifier RPAREN
      AS LPAREN projectAliasExpr (COMMA projectAliasExpr)* COMMA? RPAREN
    ;

projectAliasExpr
    : identifier COLON projectExprComparison
    ;

projectExprComparison
    : projectExpr (projectCompareOp projectExpr)*
    ;

projectCompareOp
    : EQ
    | NEQ
    | LT
    | LTE
    | GT
    | GTE
    | LIKE
    ;

projectExpr
    : stringLiteral
    | numberLiteral
    | NULL
    | projectCaseExpr
    | projectTextExpr
    | projectAttrExpr
    | projectCoalesceExpr
    | projectPositionExpr
    | projectFunctionCall
    | projectOperand
    | aliasRef
    ;

projectCaseExpr
    : CASE (WHEN expr THEN projectExpr)+ (ELSE projectExpr)? END
    ;

projectTextExpr
    : (TEXT | DIRECT_TEXT | FIRST_TEXT | LAST_TEXT)
      LPAREN tagIdentifier (WHERE expr)? (COMMA projectSelectorPos)? RPAREN
    ;

projectAttrExpr
    : (ATTR | FIRST_ATTR | LAST_ATTR)
      LPAREN tagIdentifier COMMA identifier (WHERE expr)? (COMMA projectSelectorPos)? RPAREN
    ;

projectSelectorPos
    : numberLiteral
    | FIRST
    | LAST
    ;

projectCoalesceExpr
    : COALESCE LPAREN projectExpr COMMA projectExpr (COMMA projectExpr)* RPAREN
    ;

projectPositionExpr
    : POSITION LPAREN projectExpr IN projectExpr RPAREN
    ;

projectFunctionCall
    : callableIdentifier LPAREN (projectExpr (COMMA projectExpr)*)? RPAREN
    ;

projectOperand
    : SELF DOT selfMember
    | ATTRIBUTES (DOT identifier)?
    | bareField
    | axisNameNoSelf DOT axisField
    | qualifiedOperand
    ;

qualifiedOperand
    : identifier DOT qualifiedOperandTail
    ;

qualifiedOperandTail
    : ATTRIBUTES (DOT identifier)?
    | bareField
    | axisNameNoSelf DOT axisField
    | identifier
    ;

selfMember
    : ATTRIBUTES (DOT identifier)?
    | bareField
    ;

bareField
    : TAG
    | TEXT
    | NODE_ID
    | PARENT_ID
    | SIBLING_POS
    | MAX_DEPTH
    | DOC_ORDER
    ;

axisField
    : ATTRIBUTES DOT identifier
    | bareField
    ;

axisNameNoSelf
    : PARENT
    | CHILD
    | ANCESTOR
    | DESCENDANT
    ;

aliasRef
    : identifier
    ;

flattenSelectItem
    : (FLATTEN_TEXT | FLATTEN)
      LPAREN tagIdentifier (COMMA UINT)? RPAREN
      (AS LPAREN identifier (COMMA identifier)* RPAREN)?
    ;

selfProjectionSelectItem
    : SELF DOT selfMember (AS identifier)?
    ;

trimSelectItem
    : TRIM LPAREN trimTarget RPAREN
    ;

trimTarget
    : (INNER_HTML | RAW_INNER_HTML) LPAREN tagIdentifier (COMMA innerHtmlDepthArg)? RPAREN
    | TEXT LPAREN tagIdentifier RPAREN
    | tagIdentifier DOT identifier
    ;

countSelectItem
    : COUNT LPAREN (STAR | tagIdentifier) RPAREN
    ;

summarizeSelectItem
    : SUMMARIZE LPAREN STAR RPAREN
    ;

tfidfSelectItem
    : TFIDF LPAREN tfidfArg (COMMA tfidfArg)* RPAREN
    ;

tfidfArg
    : STAR
    | tagIdentifier
    | tfidfOption
    ;

tfidfOption
    : TOP_TERMS EQ UINT
    | MIN_DF EQ UINT
    | MAX_DF EQ UINT
    | STOPWORDS EQ (ENGLISH | DEFAULT | NONE | OFF | stringLiteral | identifier)
    ;

caseProjectionSelectItem
    : projectCaseExpr (AS identifier)?
    ;

firstLastProjectionSelectItem
    : firstLastProjectExpr (AS identifier)?
    ;

firstLastProjectExpr
    : (FIRST_TEXT | LAST_TEXT)
      LPAREN tagIdentifier (WHERE expr)? (COMMA projectSelectorPos)? RPAREN
    | (FIRST_ATTR | LAST_ATTR)
      LPAREN tagIdentifier COMMA identifier (WHERE expr)? (COMMA projectSelectorPos)? RPAREN
    ;

scalarFunctionProjectionSelectItem
    : scalarProjectionFunction (AS identifier)?
    ;

scalarProjectionFunction
    : POSITION LPAREN scalarExpr IN scalarExpr RPAREN
    | ATTR LPAREN scalarNodeArg COMMA identifier RPAREN
    | DIRECT_TEXT LPAREN scalarNodeArg RPAREN
    | (CONCAT | SUBSTRING | SUBSTR | LENGTH | CHAR_LENGTH | LOCATE | REPLACE | LOWER | UPPER | LTRIM | RTRIM | COALESCE)
      LPAREN (scalarExpr (COMMA scalarExpr)*)? RPAREN
    ;

textProjectionSelectItem
    : TEXT LPAREN scalarNodeArg RPAREN (AS identifier)?
    ;

innerHtmlProjectionSelectItem
    : (INNER_HTML | RAW_INNER_HTML)
      LPAREN scalarNodeArg (COMMA innerHtmlDepthArg)? RPAREN
      (AS identifier)?
    ;

scalarNodeArg
    : SELF
    | tagIdentifier
    ;

innerHtmlDepthArg
    : numberLiteral
    | MAX_DEPTH
    ;

tagFieldListSelectItem
    : tagIdentifier LPAREN identifier (COMMA identifier)* RPAREN
    ;

tagFieldSelectItem
    : tagIdentifier DOT identifier
    ;

tagOnlySelectItem
    : tagIdentifier
    ;

excludeList
    : identifier
    | LPAREN identifier (COMMA identifier)* RPAREN
    ;

source
    : DOCUMENT sourceAlias?
    | RAW LPAREN stringLiteral RPAREN sourceAlias?
    | FRAGMENTS LPAREN (RAW LPAREN stringLiteral RPAREN | subqueryBody) RPAREN sourceAlias?
    | stringLiteral sourceAlias?
    | identifier
    ;

sourceAlias
    : AS? identifier
    ;

orderByItem
    : (identifier | COUNT) (ASC | DESC)?
    ;

toTarget
    : LIST LPAREN RPAREN
    | TABLE LPAREN tableOptionList? RPAREN
    | CSV LPAREN stringLiteral RPAREN
    | PARQUET LPAREN stringLiteral RPAREN
    | JSON LPAREN stringLiteral? RPAREN
    | NDJSON LPAREN stringLiteral? RPAREN
    ;

tableOptionList
    : tableOption (COMMA tableOption)*
    ;

tableOption
    : HEADER (EQ? (ON | OFF)?)?
    | NOHEADER
    | NO_HEADER
    | EXPORT EQ? stringLiteral
    | TRIM_EMPTY_ROWS EQ? (ON | OFF)
    | TRIM_EMPTY_COLS EQ? (OFF | TRAILING | ALL)
    | EMPTY_IS EQ? (BLANK_OR_NULL | NULL_ONLY | BLANK_ONLY)
    | STOP_AFTER_EMPTY_ROWS EQ? numberLiteral
    | FORMAT EQ? (RECT | SPARSE)
    | SPARSE_SHAPE EQ? (LONG | WIDE)
    | HEADER_NORMALIZE EQ? (ON | OFF)
    ;

expr
    : andExpr (OR andExpr)*
    ;

andExpr
    : cmpExpr (AND cmpExpr)*
    ;

cmpExpr
    : LPAREN expr RPAREN
    | EXISTS LPAREN existsAxis (WHERE expr)? RPAREN
    | scalarExpr cmpTail
    ;

existsAxis
    : SELF
    | PARENT
    | CHILD
    | ANCESTOR
    | DESCENDANT
    ;

cmpTail
    : CONTAINS (ALL | ANY)? stringList
    | HAS_DIRECT_TEXT stringLiteral
    | IN (LPAREN scalarExpr (COMMA scalarExpr)* RPAREN | scalarExpr)
    | IS NOT? NULL
    | compareOp scalarExpr
    ;

compareOp
    : EQ
    | NEQ
    | LT
    | LTE
    | GT
    | GTE
    | REGEX_MATCH
    | LIKE
    ;

scalarExpr
    : stringLiteral
    | numberLiteral
    | NULL
    | scalarFunction
    | operand
    | SELF
    ;

scalarFunction
    : POSITION LPAREN scalarExpr IN scalarExpr RPAREN
    | TEXT LPAREN scalarNodeArg RPAREN
    | DIRECT_TEXT LPAREN scalarNodeArg RPAREN
    | INNER_HTML LPAREN scalarNodeArg (COMMA innerHtmlDepthArg)? RPAREN
    | RAW_INNER_HTML LPAREN scalarNodeArg (COMMA innerHtmlDepthArg)? RPAREN
    | ATTR LPAREN scalarNodeArg COMMA identifier RPAREN
    | callableIdentifier LPAREN (scalarExpr (COMMA scalarExpr)*)? RPAREN
    ;

operand
    : SELF DOT selfMember
    | ATTRIBUTES (DOT identifier)?
    | bareField
    | axisNameNoSelf DOT axisField
    | tagIdentifier DOT qualifiedOperandTail
    | tagIdentifier
    ;

callableIdentifier
    : identifier
    | TABLE
    | SELF
    ;

tagIdentifier
    : identifier
    | TABLE
    ;

identifier
    : IDENTIFIER
    | DOC
    | LANGUAGE
    | FLATTEN_EXTRACT
    | FLATTEN_TEXT
    | FLATTEN
    | SUMMARIZE
    | TFIDF
    | TRIM
    | TEXT
    | DIRECT_TEXT
    | FIRST_TEXT
    | LAST_TEXT
    | ATTR
    | FIRST_ATTR
    | LAST_ATTR
    | INNER_HTML
    | RAW_INNER_HTML
    | COALESCE
    | POSITION
    | CONCAT
    | SUBSTRING
    | SUBSTR
    | LENGTH
    | CHAR_LENGTH
    | LOCATE
    | REPLACE
    | LOWER
    | UPPER
    | LTRIM
    | RTRIM
    | MAX_DEPTH
    | NODE_ID
    | PARENT_ID
    | SIBLING_POS
    | DOC_ORDER
    | ATTRIBUTES
    | TAG
    | PARENT
    | CHILD
    | ANCESTOR
    | DESCENDANT
    | HEADER
    | NOHEADER
    | NO_HEADER
    | EXPORT
    | TRIM_EMPTY_ROWS
    | TRIM_EMPTY_COLS
    | EMPTY_IS
    | STOP_AFTER_EMPTY_ROWS
    | FORMAT
    | SPARSE_SHAPE
    | HEADER_NORMALIZE
    | TRAILING
    | BLANK_OR_NULL
    | NULL_ONLY
    | BLANK_ONLY
    | RECT
    | SPARSE
    | LONG
    | WIDE
    | TOP_TERMS
    | MIN_DF
    | MAX_DF
    | STOPWORDS
    | NONE
    | OFF
    | ON
    | ENGLISH
    | DEFAULT
    | FIRST
    | LAST
    ;

stringList
    : stringLiteral
    | LPAREN stringLiteral (COMMA stringLiteral)* RPAREN
    ;

stringLiteral
    : STRING
    ;

numberLiteral
    : UINT
    ;
