# Task 01

1) Goal
Get a baseline list of all offer rows.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query "SELECT * FROM doc LIMIT 8;"
```

3) Try in REPL
```sql
SELECT div(node_id, tag, max_depth)
FROM doc
WHERE attributes.data-testid = 'offer'
ORDER BY node_id;
```

4) Your task
Return only rows with `data-testid='offer'`.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/01_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag,max_depth` and 3 data rows.

7) Hints
- Start from stable attributes, not classes.
- Keep sort explicit.
- Include `max_depth` to inspect structure quickly.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/01_solution.sql`
