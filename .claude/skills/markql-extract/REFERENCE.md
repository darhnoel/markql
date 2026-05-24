# MarkQL Canonical Query Shapes

Every shape below has been verified against the live CLI on real HTML. Use these as templates; do not invent variants without checking the gotchas table in SKILL.md.

## Conventions

- `n` — chosen row alias for `FROM doc AS n`.
- `<tag>` — a literal tag name (`div`, `a`, `h3`, …). Never an alias.
- `<id>` — a node_id integer.
- Probes use `LIMIT 5` or `COUNT(*)` to stay cheap. Final extractions remove the bound.

## 1. Sanity / counts

```sql
SELECT COUNT(*) FROM doc;
SELECT COUNT(*) FROM doc WHERE tag = '<tag>';
SELECT COUNT(*) FROM doc WHERE attributes.<attr_name> IS NOT NULL;
SELECT COUNT(*) FROM doc WHERE text LIKE '%<keyword>%';
```

## 2. Anchor by text (find the reference node)

```sql
-- DIRECT_TEXT(self) only matches text directly inside the node, not in descendants.
-- 'text' field matches aggregated descendant text.
SELECT COUNT(*) FROM doc WHERE DIRECT_TEXT(self) LIKE '%<anchor>%';
SELECT doc.node_id, doc.tag, doc.parent_id
  FROM doc
  WHERE DIRECT_TEXT(self) LIKE '%<anchor>%'
  LIMIT 5;
```

If `DIRECT_TEXT(self)` returns 0 hits, the keyword is in a nested child — switch to `WHERE text LIKE '%<anchor>%'` and add `tag = '<t>'` to narrow.

## 3. Row probe (inspect candidate rows with attributes)

```sql
SELECT n.node_id, n.tag, n.parent_id, ATTR(<tag>, class), ATTR(<tag>, id)
  FROM doc AS n
  WHERE n.tag = '<tag>'
  LIMIT 5;
```

Key constraint: `ATTR()` must receive a literal `<tag>`, never the alias. `ATTR(n, class)` returns NULL silently.

## 4. Walking the tree

```sql
-- Children of a node
SELECT n.node_id, n.tag, ATTR(<tag>, class)
  FROM doc AS n
  WHERE n.parent_id = <id>
  LIMIT 30;

-- All descendants
SELECT n.node_id, n.tag FROM doc AS n WHERE ancestor.node_id = <id> LIMIT 50;

-- Get this node's parent
SELECT n.node_id, n.tag, n.parent_id FROM doc AS n WHERE n.node_id = <id>;
```

Subqueries inside scalar predicates (`WHERE x = (SELECT ...)`) are unsupported — look up the value with a separate probe and inline the literal.

## 5. Attribute access — where each form belongs

| Context | Form |
|---|---|
| `WHERE` clause | `attributes.<name>` works for `=`, `<>`, `LIKE`, `CONTAINS`, `IS NULL`. |
| `WHERE` with multi-value (class lists) | `attributes.class CONTAINS 'x'`, `CONTAINS ANY ('a','b')`, `CONTAINS ALL (...)`. |
| Axis: child/parent/etc | `child.attr.<name>`, `parent.attr.<name>`, `descendant.attr.<name>`. |
| `SELECT` projection | `<tag>.<name>` (e.g. `a.href`) when the row is that tag, **or** `ATTR(<tag>, <name>)`. |
| Inside `PROJECT(...)` | `ATTR(<tag>, <name> WHERE <pred>)`. Never `ATTR(self, ...)`. |

## 6. Text suppliers — what each returns

| Form | Scope | Returns |
|---|---|---|
| `TEXT(<tag>)` outside `PROJECT` | filtered row | Aggregated text of node + descendants. Requires non-tag `WHERE` predicate. |
| `TEXT(<tag>)` inside `PROJECT(n)` | row + descendants | **Direct** text of the matched child only. Nested element content is dropped — drill into the inner element. |
| `DIRECT_TEXT(self)` in `WHERE` | self | Only the node's own text children, no descendants. |
| `INNER_HTML(<tag>)` | row | Minified inner HTML. Use `RAW_INNER_HTML` to preserve spacing. Requires non-tag `WHERE`. |
| `FLATTEN(n) AS (a,b,c)` | row | Ordered text slices, positional. Brittle on variant rows — prefer `PROJECT` once shape is known. |

## 7. Stable per-row extraction (the main shape)

```sql
SELECT n.node_id,
PROJECT(n) AS (
  field_a: TRIM(TEXT(<tag_a> WHERE <pred_a>)),
  field_b: TRIM(TEXT(<tag_b> WHERE parent.tag = '<pt>')),
  field_c: COALESCE(
    ATTR(<tag>, <attr> WHERE <pred>),
    TEXT(<tag> WHERE <pred>)
  ),
  field_d: CASE WHEN <bool_expr> THEN <val> ELSE <val> END
)
FROM doc AS n
WHERE n.tag = '<row_tag>'
  AND n.parent_id = <container_id>     -- preferred row anchor
  AND EXISTS(descendant WHERE attr.<a> = '<v>')   -- structural guard
ORDER BY node_id;                       -- bare field; no alias prefix
```

Inside `PROJECT`, later fields can reference earlier aliases by name.

## 8. ORDER BY restrictions

```sql
ORDER BY node_id;          -- ok
ORDER BY doc_order DESC;   -- ok
ORDER BY n.node_id;        -- ERROR — alias prefix not allowed
ORDER BY title;            -- ERROR — projected aliases not allowed
```

Allowed bare fields only: `node_id`, `tag`, `text`, `parent_id`, `sibling_pos`, `max_depth`, `doc_order`.

## 9. Filter operators — supported and not

| ✓ Works | ✗ Does NOT work |
|---|---|
| `=`, `<>`, `!=`, `<`, `<=`, `>`, `>=` | `NOT (expr)` — wrap-style negation |
| `IN (...)`, `LIKE '%x%'` | `NOT LIKE`, `NOT IN` |
| `IS NULL`, `IS NOT NULL` | subqueries in scalar position |
| `~` (regex), `CONTAINS`, `CONTAINS ALL/ANY` | `GROUP BY`, `HAVING` |
| `EXISTS(axis [WHERE expr])` | aggregate-in-predicate |
| `POSITION('s' IN str)`, `LENGTH(str)` | |

To express "not-X" without `NOT`: use `<>`, or rewrite with positive `LIKE` on a different anchor, or check `IS NULL`.

## 10. String functions

`CONCAT`, `SUBSTRING`/`SUBSTR`, `LENGTH`/`CHAR_LENGTH` (UTF-8 bytes), `POSITION`/`LOCATE`, `REPLACE`, `REGEX_REPLACE`, `LOWER`, `UPPER`, `LTRIM`, `RTRIM`, `TRIM`, `DIRECT_TEXT`. All available in `SELECT`, `WHERE`, and `PROJECT` field expressions.

```sql
-- Numeric normalization for export
PROJECT(n) AS (
  price_num: TRIM(REPLACE(REPLACE(TEXT(span WHERE attr.role = 'text'), '¥', ''), ',', ''))
)
```

## 11. Output tails

```sql
... TO LIST();                       -- single-column JSON array to stdout
... TO JSON();        ... TO JSON('out.json');
... TO NDJSON();      ... TO NDJSON('out.ndjson');
... TO CSV('out.csv');
... TO PARQUET('out.parquet');

-- HTML <table> tag extraction (different from generic row export):
SELECT table FROM doc WHERE id = '<id>' TO TABLE(EXPORT='out.csv');
SELECT table FROM doc TO TABLE(TRIM_EMPTY_ROWS=ON, TRIM_EMPTY_COLS=TRAILING);
```

## 12. Self-discovery commands

```sql
SHOW FUNCTIONS;
SHOW AXES;
SHOW OPERATORS;
DESCRIBE doc;
DESCRIBE language;
```

Use these before guessing syntax. They return small, structured outputs (cheap on tokens).

## 13. CLI cheats

```bash
markql --lint "<query>" --format json    # parse + validate, no execute
markql --query "<q>" --input page.html   # one-shot
markql --query-file q.sql --input page.html
markql --input page.html --write-mqd cache/page.mqd        # cache parsed doc
markql --query-file q.sql --input cache/page.mqd           # reuse cache
```

Exit codes: `0` success, `1` parse/runtime error, `2` CLI/IO failure.
