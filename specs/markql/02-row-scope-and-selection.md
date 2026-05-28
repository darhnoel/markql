# Row Scope and Selection

Status: draft skeleton

This file defines row survival and name binding.

## SCOPE-001: Two-Stage Model

Outer `WHERE` decides whether the current row survives. Field expressions do not decide row survival unless they appear inside the outer `WHERE`.

## SCOPE-002: Lexical Scoping

Bare fields use standard lexical scoping. The innermost row scope wins.

Example:

```sql
FROM doc AS article
WHERE tag = 'article'
  AND EXISTS(descendant WHERE tag = 'a')
```

Inside `EXISTS(descendant ...)`, bare `tag` binds to the descendant candidate. To reference the outer row, use the outer alias.

## SCOPE-003: Alias Canonicality

Aliases are canonical row references. Tag identity belongs in predicates:

```sql
FROM doc AS n
WHERE tag = 'section'
```

Avoid aliases that look like tag constraints when they are only row names.

## SCOPE-004: Legacy Tag-As-Row Migration

Legacy tag-as-row forms imply row selection. Migration should make the tag constraint explicit:

```sql
SELECT n.node_id
FROM doc AS n
WHERE tag = 'section'
```

