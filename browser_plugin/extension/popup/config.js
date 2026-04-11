export const AGENT_URL = "http://127.0.0.1:7337/v1/query";
export const STORAGE_KEY_TOKEN = "markqlAgentToken";
export const STORAGE_KEY_QUERY = "markqlLastQuery";
export const STORAGE_KEY_QUERY_COLLAPSED = "markqlQueryCollapsed";
export const STORAGE_KEY_SNAPSHOT = "markqlSnapshotHtml";
export const STORAGE_KEY_SNAPSHOT_SCOPE = "markqlSnapshotScope";
export const STORAGE_KEY_SNAPSHOT_DOCS = "markqlSnapshotDocs";
export const STORAGE_KEY_SNAPSHOT_ID = "markqlSnapshotId";
export const STORAGE_KEY_LINT = "markqlLintEnabled";
export const LEGACY_STORAGE_KEY_TOKEN = "xsqlAgentToken";
export const LEGACY_STORAGE_KEY_QUERY = "xsqlLastQuery";
export const LEGACY_STORAGE_KEY_SNAPSHOT = "xsqlSnapshotHtml";
export const PRIMARY_CAPTURE_SCOPE = "full";
export const FALLBACK_CAPTURE_SCOPE = "main";

export const SQL_KEYWORDS = new Set([
  "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "IN", "AS", "LIMIT",
  "ORDER", "BY", "ASC", "DESC", "EXISTS", "IS", "NULL", "CONTAINS",
  "ANY", "ALL", "LIKE", "EXCLUDE",
  "WITH", "JOIN", "LEFT", "CROSS", "LATERAL", "ON",
  "TO", "TABLE", "LIST", "CSV", "PARQUET", "JSON", "NDJSON",
  "RAW", "FRAGMENTS", "PARSE",
  "SHOW", "DESCRIBE", "INPUT", "INPUTS", "FUNCTIONS", "AXES", "OPERATORS", "LANGUAGE",
  "CASE", "WHEN", "THEN", "ELSE", "END",
  "HEADER", "NOHEADER", "NO_HEADER", "DEFAULT",
  "ON", "OFF",
  "TRIM_EMPTY_ROWS", "TRIM_EMPTY_COLS", "EMPTY_IS", "STOP_AFTER_EMPTY_ROWS",
  "FORMAT", "SPARSE_SHAPE", "HEADER_NORMALIZE",
  "BLANK_OR_NULL", "NULL_ONLY", "BLANK_ONLY", "TRAILING",
  "RECT", "SPARSE", "LONG", "WIDE",
  "DOC", "DOCUMENT",
  "ENGLISH", "NONE", "MAX_DEPTH"
]);

export const SQL_FUNCTIONS = new Set([
  "TEXT", "DIRECT_TEXT", "INNER_HTML", "RAW_INNER_HTML",
  "FLATTEN", "FLATTEN_TEXT", "PROJECT", "FLATTEN_EXTRACT",
  "ATTR", "COUNT", "SUMMARIZE", "TFIDF",
  "COALESCE", "CONCAT", "SUBSTRING", "SUBSTR", "LENGTH", "CHAR_LENGTH",
  "POSITION", "LOCATE", "REPLACE", "REGEX_REPLACE",
  "LOWER", "UPPER", "TRIM", "LTRIM", "RTRIM",
  "FIRST_TEXT", "LAST_TEXT", "FIRST_ATTR", "LAST_ATTR",
  "HAS_DIRECT_TEXT"
]);
