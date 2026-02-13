# Task 03

Story beat: target one row intentionally, not by accident.

1) Goal
Find the row containing `Tokyo` using outer `WHERE` text matching.

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
  AND text LIKE '%Tokyo%';
```

4) Your task
Return the single matching section row in deterministic order.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/03_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 1 data row.

7) Hints
- Use `%Tokyo%` with `LIKE`.
- Keep `tag='section'` to avoid accidental matches.
- Add `ORDER BY node_id`.

8) Solution reference
- `docs/exercises/01_basics_select_where/tasks/03_solution.sql`
