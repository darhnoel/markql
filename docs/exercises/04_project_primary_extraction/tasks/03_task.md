# Task 03

1) Goal
Extract first and last badges from repeated badge spans.

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
         first_badge: FIRST_TEXT(span WHERE parent.attributes.class = 'badges'),
         last_badge: LAST_TEXT(span WHERE parent.attributes.class = 'badges')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Return `first_badge` and `last_badge` for each flight row.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/03_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,first_badge,last_badge`.

7) Hints
- Scope badges with `parent.attributes.class = 'badges'`.
- Use FIRST/LAST to avoid positional drift.
- Keep row gate at flight rows.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/03_solution.sql`
