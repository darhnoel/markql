# Grammar

Status: draft skeleton

This file defines accepted MarkQL syntax. It should be kept aligned with `docs/book/appendix-grammar.md` and the parser implementation.

## GRAMMAR-001: Query Shape

MarkQL queries are SQL-style statements over HTML node streams and derived relations.

Normative details to fill:

- `SELECT`
- `FROM`
- `JOIN`
- `WHERE`
- `ORDER BY`
- `LIMIT`
- output sinks

## GRAMMAR-002: SQL-Faithful Alias Surface

Aliases are canonical row references in the SQL-faithful surface.

The grammar must support narrow node-row projection:

```sql
SELECT n.*
FROM doc AS n
WHERE tag = 'section';
```

`SELECT alias.*` means "project the current node row for this alias." It is not yet a general SQL table wildcard.

## GRAMMAR-003: Legacy Forms

Legacy forms remain compatibility inputs until migration removes them from docs/examples:

- `SELECT self`
- `self.<field>`
- bare tag-as-row selection
- tag field-list selection such as `SELECT div(node_id, tag)`

The SQL-faithful canonical form must be documented separately from these legacy forms.

