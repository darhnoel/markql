# Task 02

1) Goal
Narrow to gold offers only.

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
  AND attributes.data-kind = 'gold'
ORDER BY node_id;
```

4) Your task
Keep gold offers and drop silver offers.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/02_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 2 data rows.

7) Hints
- Add one filter at a time.
- Keep `data-testid` and `data-kind` together.
- Do not rely on class suffixes.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/02_solution.sql`
