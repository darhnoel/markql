# Task 05

1) Goal
Use `CASE` inside PROJECT to classify rows.

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
         class_bucket: CASE
           WHEN attributes.data-kind = 'flight' THEN 'transport'
           ELSE 'other'
         END
       )
FROM doc
WHERE attributes.class CONTAINS 'result'
ORDER BY node_id;
```

4) Your task
Add `class_bucket` while keeping row scope unchanged.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/05_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,class_bucket`.

7) Hints
- CASE returns one scalar per row.
- Keep CASE local to PROJECT.
- Do not split into multiple queries.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/05_solution.sql`
