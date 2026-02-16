# Task 09 (Conversion Drill)

1) Goal
Rewrite a FLATTEN-style extraction into PROJECT with named columns.

2) Open the fixture
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query "SELECT section.node_id, FLATTEN(section) AS (v1,v2,v3,v4,v5,v6,v7) FROM doc WHERE attributes.data-kind='flight' ORDER BY node_id;"
```

3) Try in REPL
```sql
-- Target rewrite uses named fields:
SELECT section.node_id,
       PROJECT(section) AS (
         city: TEXT(h3),
         summary: TEXT(p WHERE attributes.class = 'summary'),
         stop_text: TEXT(span WHERE attributes.class CONTAINS 'stops'),
         price_text: TEXT(span WHERE attributes.role = 'text')
       )
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

4) Your task
Convert positional flatten output into semantic PROJECT columns.

5) Check your output
```bash
./build/markql \
  --input docs/exercises/04_project_primary_extraction/fixtures/page.html \
  --query-file docs/exercises/04_project_primary_extraction/tasks/09_solution.sql
```

6) Expected output
CSV excerpt: header `node_id,city,summary,stop_text,price_text`.

7) Hints
- Keep one output row per flight card.
- Name fields by meaning, not position.
- Supplier predicates belong inside each field expression.

8) Solution reference
- `docs/exercises/04_project_primary_extraction/tasks/09_solution.sql`
