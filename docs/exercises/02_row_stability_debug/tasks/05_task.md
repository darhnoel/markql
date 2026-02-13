# Task 05

1) Goal
Find one specific offer using normalized text matching.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query "SELECT div(node_id, tag) FROM doc WHERE attributes.data-testid='offer' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND LOWER(text) LIKE '%terra prospect%'
ORDER BY node_id;
```

4) Your task
Return only the `Terra Prospect` row.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/05_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 1 data row.

7) Hints
- Normalize with `LOWER(...)`.
- Use `%...%` around your phrase.
- Keep deterministic ordering anyway.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/05_solution.sql`
