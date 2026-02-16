# Task 02

Story beat: narrow from generic rows to real flight rows using stable evidence.

1) Goal
Keep only flight rows using a stable attribute filter.

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
WHERE tag = 'section';

SELECT section(node_id, tag)
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Filter with `attributes.data-kind = 'flight'`.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/02_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,tag` and 2 data rows.

7) Hints
- Prefer `data-*` over cosmetic class names.
- Keep `ORDER BY node_id`.
- You can omit `tag='section'` if attribute is unique.

8) Solution reference
- `docs/exercises/01_basics_select_where/tasks/02_solution.sql`
