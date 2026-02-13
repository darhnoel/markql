# Task 06

1) Goal
Build the final stable gold-offer row gate.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query "SELECT div(node_id, tag, max_depth) FROM doc WHERE attributes.data-testid='offer' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT div(node_id, tag, max_depth)
FROM doc
WHERE attributes.data-testid = 'offer'
  AND attributes.data-kind = 'gold'
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
  AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
ORDER BY node_id;
```

4) Your task
Combine all stability conditions into one row filter.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/02_row_stability_debug/fixtures/page.html \
  --query-file docs/exercises/02_row_stability_debug/tasks/06_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag,max_depth` and 1 data row.

7) Hints
- Keep each condition on its own line.
- Use both stable attributes and structural checks.
- Avoid class-only row gates.

8) Solution reference
- `docs/exercises/02_row_stability_debug/tasks/06_solution.sql`
