# Task 04

1) Goal
Remove teaser rows while keeping only real gold offers.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query "SELECT div(node_id, tag) FROM doc WHERE attributes.data-kind='gold' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT div(node_id, tag)
FROM doc
WHERE attributes.data-kind = 'gold'
  AND attributes.data-testid = 'offer'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
ORDER BY node_id;
```

4) Your task
Use structural evidence to reject teaser cards.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/04_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 2 data rows.

7) Hints
- Teasers can share `data-kind`.
- Add `data-testid='offer'` and one must-have descendant.
- Validate with small projections.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/04_solution.sql`
