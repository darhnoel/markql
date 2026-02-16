# Task 05

Story beat: add structural proof so weak rows do not slip in.

1) Goal
Require each kept section row to have a direct child price span.

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
  AND EXISTS(child WHERE tag = 'span');

SELECT section(node_id, tag)
FROM doc
WHERE tag = 'section'
  AND EXISTS(child WHERE tag = 'span' AND attributes.class = 'price')
ORDER BY node_id;
```

4) Your task
Use `EXISTS(child ...)` to keep rows with a price span only.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/05_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 3 data rows.

7) Hints
- Put `EXISTS(...)` in outer `WHERE`.
- Start broad (`tag='span'`) then tighten by class.
- Keep `ORDER BY node_id`.

8) Solution reference
- `docs/exercises/01_basics_select_where/tasks/05_solution.sql`
