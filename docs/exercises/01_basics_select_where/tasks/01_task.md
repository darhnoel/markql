# Task 01

Story beat: establish the row unit before doing any extraction.

1) Goal
Find the first three `section` rows and display `node_id` + `tag`.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query "SELECT * FROM doc LIMIT 5;"
```

3) Try in REPL
```sql
SELECT section(node_id, tag)
FROM doc;

SELECT section(node_id, tag)
FROM doc
ORDER BY node_id
LIMIT 3;
```

4) Your task
Add the explicit `WHERE tag = 'section'` row gate.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/01_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 3 data rows.

7) Hints
- Keep `ORDER BY node_id` for deterministic order.
- Add `LIMIT 3` after sorting.
- Row gate belongs in outer `WHERE`.

8) Solution reference
- `docs/exercises/01_basics_select_where/tasks/01_solution.sql`
