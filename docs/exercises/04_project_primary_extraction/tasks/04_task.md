# Task 04

1) Goal
Handle missing price safely with `COALESCE`.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section(node_id, tag) FROM doc WHERE attributes.class CONTAINS 'result' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         price_final: COALESCE(TEXT(span WHERE attributes.role = 'text'), 'N/A')
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
ORDER BY node_id;
```

4) Your task
Include all result rows and make missing price explicit.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/04_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,price_final`.

7) Hints
- Keep outer row set broad on purpose.
- Use field-level fallback, not row deletion.
- Check hotel row behavior.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/04_solution.sql`
