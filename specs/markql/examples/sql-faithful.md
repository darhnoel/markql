# SQL-Faithful Examples

Status: draft skeleton

This file collects canonical examples for the SQL-faithful MarkQL surface.

## EXAMPLE-SQL-001: Row Alias and Explicit Tag Predicate

```sql
SELECT n.node_id,
       PROJECT(n) AS (
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc AS n
WHERE tag = 'section'
  AND attributes.data-kind = 'flight';
```

## EXAMPLE-SQL-002: Current Node Row Projection

```sql
SELECT n.*
FROM doc AS n
WHERE tag = 'section'
ORDER BY node_id
LIMIT 1;
```

## EXAMPLE-SQL-003: Nested Scope

```sql
SELECT n.node_id
FROM doc AS n
WHERE tag = 'article'
  AND EXISTS(descendant WHERE tag = 'a' AND attributes.href IS NOT NULL);
```

Inside `EXISTS(descendant ...)`, bare fields bind to the descendant candidate.

## EXAMPLE-SQL-004: Cross-Scope Reference

```sql
SELECT n.node_id
FROM doc AS n
WHERE tag = 'article'
  AND EXISTS(descendant WHERE attributes.data-owner = n.attributes.id);
```

The outer row is referenced explicitly as `n`.

