# Task 07 (Anti-example 2)

1) Goal
Fix FLATTEN context loss for badge extraction.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section.node_id, FLATTEN(section) AS (v1,v2,v3,v4,v5,v6,v7) FROM doc WHERE attributes.data-kind='flight' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         badges_pair: CONCAT(
           COALESCE(FIRST_TEXT(span WHERE parent.attributes.class = 'badges'), 'none'),
           '|',
           COALESCE(LAST_TEXT(span WHERE parent.attributes.class = 'badges'), 'none')
         )
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Return a stable badge pair per flight row.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/07_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,badges_pair`.

7) Hints
- Scope badge supplier by parent class.
- Use `COALESCE` for missing second badge.
- Keep output one row per flight.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/07_solution.sql`
