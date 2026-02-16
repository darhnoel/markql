# Task 08 (Anti-example 3)

1) Goal
Replace awkward positional extraction with explicit numeric price parsing.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section.node_id, FLATTEN(section) AS (v1,v2,v3,v4,v5,v6,v7) FROM doc WHERE attributes.data-kind='flight' ORDER BY node_id;"
```

3) Try in REPL
```sql
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         stop_text: TEXT(span WHERE attributes.class CONTAINS 'stops'),
         price_num: TRIM(REPLACE(REPLACE(TEXT(span WHERE attributes.role = 'text'), '¥', ''), ',', ''))
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Produce clean numeric price strings without positional assumptions.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/08_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,stop_text,price_num`.

7) Hints
- Parse price from semantic supplier node.
- Strip `¥` and commas in-query.
- Keep row gate stable and simple.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/08_solution.sql`
