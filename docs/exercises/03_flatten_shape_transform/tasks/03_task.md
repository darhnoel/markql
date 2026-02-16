# Task 03

1) Goal
Flatten each `<li>` into item-level columns.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query "SELECT li(node_id, tag) FROM doc WHERE parent.attributes.class='line-items' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT li.node_id,
       FLATTEN(li) AS (item_name, item_qty)
FROM doc
WHERE parent.attributes.class = 'line-items'
ORDER BY node_id;
```

4) Your task
Return one row per line item with name and quantity.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/03_flatten_shape_transform/fixtures/page.html \
  --query-file docs/exercises/03_flatten_shape_transform/tasks/03_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,item_name,item_qty`.

7) Hints
- Keep row scope at `li` for item-level output.
- `FLATTEN(li)` here maps cleanly to two columns.
- Keep this contrast in mind before using FLATTEN broadly.

8) Solution reference
- `docs/exercises/03_flatten_shape_transform/tasks/03_solution.sql`
