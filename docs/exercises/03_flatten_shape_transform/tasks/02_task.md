# Task 02

1) Goal
Flatten each line-items container into positional chunks.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query "SELECT ul(node_id, tag) FROM doc WHERE attributes.class='line-items' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT ul.node_id,
       FLATTEN(ul) AS (chunk1, chunk2, chunk3, chunk4)
FROM doc
WHERE attributes.class = 'line-items'
ORDER BY node_id;
```

4) Your task
Return ordered flattened values for each line-items list.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query-file docs/exercises/03_flatten_shape_transform/tasks/02_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,chunk1,chunk2,chunk3,chunk4`.

7) Hints
- Keep row scope and flatten tag aligned (`ul.node_id` with `FLATTEN(ul)`).
- Row scope changed from shipment to list container.
- Column positions follow DOM order.
- Missing values become empty/null slots.

8) Solution reference
- `docs/exercises/03_flatten_shape_transform/tasks/02_solution.sql`
