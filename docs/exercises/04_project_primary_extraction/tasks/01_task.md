# Task 01

1) Goal
Extract city and price from each flight row with minimal PROJECT.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section(node_id, tag) FROM doc WHERE attributes.data-kind='flight' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Return one row per flight with `city` and `price_text`.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/01_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,price_text`.

7) Hints
- Keep row filter outside PROJECT.
- Keep field supplier filters inside PROJECT fields.
- Sort by `node_id`.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/01_solution.sql`
