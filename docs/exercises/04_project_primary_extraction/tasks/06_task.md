# Task 06 (Anti-example 1)

1) Goal
Fix a FLATTEN-duplication pattern by using PROJECT.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section.node_id, FLATTEN(section) AS (v1,v2,v3,v4,v5,v6,v7) FROM doc WHERE attributes.class CONTAINS 'result' ORDER BY node_id;"
```

3) Try in REPL
```sql
-- Broken idea (positional duplication):
SELECT section.node_id, FLATTEN(section) AS (v1, v2, v3, v4, v5, v6, v7)
FROM doc
WHERE attributes.class CONTAINS 'result'
ORDER BY node_id;

-- Fix:
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
  AND EXISTS(descendant WHERE attributes.role = 'text')
ORDER BY node_id;
```

4) Your task
Use PROJECT to keep one stable row per priced card.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/06_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,price_text` and only priced rows.

7) Hints
- Gate rows with `EXISTS(descendant WHERE attributes.role = 'text')`.
- Use named fields, not positional flatten slots.
- Keep ordering deterministic.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/06_solution.sql`
