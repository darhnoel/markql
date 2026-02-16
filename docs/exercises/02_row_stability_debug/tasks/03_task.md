# Task 03

1) Goal
Keep only offer rows that actually have a price node.

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
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
ORDER BY node_id;
```

4) Your task
Drop incomplete offers by requiring price existence.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/03_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 2 data rows.

7) Hints
- Use `descendant` instead of `child` unless depth is guaranteed.
- Match semantic class fragment (`price-main`).
- Keep deterministic order.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/03_solution.sql`
