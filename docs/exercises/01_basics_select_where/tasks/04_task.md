# Task 04

Story beat: control result size so debugging stays cheap and clear.

1) Goal
Use `LIMIT` to inspect only the first section row.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query "SELECT section(node_id, tag) FROM doc WHERE tag='section' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT section(node_id, tag)
FROM doc
WHERE tag = 'section'
ORDER BY node_id
LIMIT 1;
```

4) Your task
Keep exactly one row in output.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/04_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 1 data row.

7) Hints
- `LIMIT` should be the last logical clause.
- Determinism comes from `ORDER BY` before `LIMIT`.
- Keep row filter simple.

8) Solution reference
- `docs/exercises/01_basics_select_where/tasks/04_solution.sql`
