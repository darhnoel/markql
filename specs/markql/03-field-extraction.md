# Field Extraction

Status: draft skeleton

This file defines field expressions and supplier selection.

## EXTRACT-001: Field Boundary

Field expressions compute values for rows that already survived the outer `WHERE`.

## EXTRACT-002: PROJECT

`PROJECT(row_ref) AS (...)` extracts named fields from the row reference.

Canonical form:

```sql
SELECT n.node_id,
       PROJECT(n) AS (
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc AS n
WHERE tag = 'section';
```

## EXTRACT-003: Nested Supplier Scope

Inside supplier selectors such as `TEXT(span WHERE ...)`, bare fields bind to the supplier candidate.

Outer row references must be qualified.

## EXTRACT-004: FLATTEN

Normative `FLATTEN` and `FLATTEN_EXTRACT` behavior remains to be filled from the current book chapters and tests.

