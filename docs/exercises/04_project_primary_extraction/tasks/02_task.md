# Task 02

1) Goal
Add stop text as a third projected field.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/01_solution.sql
```

3) Try in REPL
```sql
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         stop_text: TEXT(span WHERE attributes.class CONTAINS 'stops'),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Add `stop_text` without changing row scope.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/02_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,stop_text,price_text`.

7) Hints
- Do not move stop logic into outer `WHERE`.
- Use a class-based supplier predicate inside field expression.
- Keep row gate unchanged.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/02_solution.sql`
