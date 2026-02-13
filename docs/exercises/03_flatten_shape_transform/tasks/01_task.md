# Task 01

1) Goal
Flatten each shipment subtree into positional text chunks.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query "SELECT div(node_id, tag) FROM doc WHERE attributes.class='shipment' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT div.node_id,
       FLATTEN(div) AS (chunk1, chunk2, chunk3, chunk4, chunk5)
FROM doc
WHERE attributes.class = 'shipment'
ORDER BY node_id;
```

4) Your task
Produce one row per shipment with flattened positional chunks.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query-file docs/exercises/03_flatten_shape_transform/tasks/01_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,chunk1,chunk2,chunk3,chunk4,chunk5`.

7) Hints
- Keep row scope and flatten tag aligned (`div.node_id` with `FLATTEN(div)`).
- FLATTEN columns are positional, not semantic.
- Use `ORDER BY node_id`.

8) Solution reference
- `docs/exercises/03_flatten_shape_transform/tasks/01_solution.sql`
