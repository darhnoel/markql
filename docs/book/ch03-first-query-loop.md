# Chapter 3: First Query Loop

## TL;DR
Use a repeatable loop: inspect rows, narrow rows, extract one value, then scale to a full schema. This sequence is faster than writing one big query and guessing where it failed.

## What is the first query loop?
The first query loop is a repeatable debugging routine for unfamiliar HTML: map rows, constrain rows, extract one field, then scale to full schema. It is a workflow pattern, not syntax. The syntax is simple; the discipline is what prevents wasted hours.

It matters because extraction bugs usually come from skipping steps. Users often jump straight to a rich `PROJECT` and then guess why outputs are empty or null. The first query loop creates checkpoints where each stage is verifiable. That makes failures local and fixes small.

This may feel slower at first because you run more small queries. In practice it is faster, because each query has one purpose and one testable expectation. Once the loop becomes habit, your extraction development feels less like trial-and-error and more like compilation.

> ### Note: Think in checkpoints, not in final query shape
> A final production query is the *last* artifact. The first artifact is evidence: “these are the rows,” then “these are the values.” If you cannot explain your query at each checkpoint, you are probably carrying an incorrect scope assumption.

## Rules
- Start with `SELECT * ... LIMIT` to see structural reality.
- Add only one predicate family at a time (`tag`, then `EXISTS`, then text/attr checks).
- Add only one extracted field first; verify it.
- Use deliberate failures to validate mental model boundaries.
- Keep each step reproducible from CLI commands.

## Alias convention (recommended style)

This is a style recommendation for readability, especially once your queries grow into multiple CTEs.

- Use `node_<semantic>` when aliasing DOM node rows.
- Use `r_<semantic>` when aliasing CTE/derived rows.

Example:

```sql
WITH r_links AS (
  SELECT node_link.href
  FROM doc AS node_link
  WHERE node_link.tag = 'a'
    AND node_link.href IS NOT NULL
)
SELECT r_links.href
FROM r_links;
```

## Scope

```text
Checkpoint A: row map
  FROM doc -> visible node stream

Checkpoint B: row gate
  WHERE ... -> kept subset

Checkpoint C: one field
  TEXT/ATTR on kept rows
```

```text
Result confidence grows in layers:
  structure confidence -> filter confidence -> value confidence
```

## Listing 3-1: Checkpoint A (row map)

<!-- VERIFY: ch03-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT * FROM doc LIMIT 8;" \
  --input docs/fixtures/basic.html
```

Observed output (trimmed):

```json
[
  {"node_id":0,"tag":"html",...},
  {"node_id":1,"tag":"body",...},
  {"node_id":2,"tag":"main",...},
  ...
]
```

The purpose is not completeness. The purpose is orientation: which tags exist, what ids/classes exist, and what row metadata you can depend on.

## Listing 3-2: Checkpoint B (row gate)

<!-- VERIFY: ch03-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, section.tag FROM doc WHERE attributes.data-kind IN ('flight') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"tag":"section"},
  {"node_id":11,"tag":"section"}
]
```

Now row scope is stable: two rows. Any field-level oddity from now on is likely stage-2 logic, not row gating.

## Listing 3-3: Deliberate failure at export checkpoint
Naive query:

```sql
SELECT a.href, a.tag FROM doc WHERE href IS NOT NULL TO LIST();
```

<!-- VERIFY: ch03-listing-3-fail -->
```bash
# EXPECT_FAIL: TO LIST() requires a single projected column
./build/markql --mode plain --color=disabled \
  --query "SELECT a.href, a.tag FROM doc WHERE href IS NOT NULL TO LIST();" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: TO LIST() requires a single projected column
```

This is a useful boundary failure. `TO LIST()` is intentionally scalar-list oriented. If you need two or more columns, use table output, `TO CSV`, `TO JSON`, or `TO NDJSON`.

## Listing 3-4: Checkpoint C (scalar extraction done correctly)

<!-- VERIFY: ch03-listing-4 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT a.href FROM 'docs/fixtures/basic.html' WHERE href IS NOT NULL TO LIST();"
```

Observed output:

```json
[
  "/home",
  "/about"
]
```

At this point, the first query loop has done its job. You now have controlled row scope and controlled value shape. Larger chapter examples are just this loop repeated with richer expressions.

## Listing 3-5: Canonical current-row node projection (`SELECT self`)

<!-- VERIFY: ch03-listing-5 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT self FROM doc AS node_x WHERE node_x.tag = 'section' ORDER BY node_id LIMIT 1;" \
  --input docs/fixtures/basic.html
```

## Listing 3-6: Compatibility old form still executes

<!-- VERIFY: ch03-listing-6 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "WITH r_rows AS (SELECT n.node_id AS row_id FROM doc AS n WHERE n.tag = 'tr') SELECT node_td.tag FROM r_rows AS r_row CROSS JOIN LATERAL (SELECT node_td FROM doc AS node_td WHERE node_td.parent_id = r_row.row_id AND node_td.tag = 'td') AS node_td ORDER BY node_id LIMIT 1;" \
  --input docs/fixtures/basic.html
```

## Listing 3-7: Lint warning + migration help for alias-as-value

<!-- VERIFY: ch03-listing-7 -->
```bash
out="$(./build/markql --lint "SELECT node_td FROM doc AS node_td WHERE node_td.tag = 'td'")"
echo "$out"
grep -F "WARNING[MQL-LINT-0001]" <<<"$out"
grep -F "Selecting the FROM alias as a value is ambiguous" <<<"$out"
grep -F "Use SELECT self to return the current node" <<<"$out"
```

## Listing 3-8: Nested LATERAL with canonical `SELECT self`

<!-- VERIFY: ch03-listing-8 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "WITH r_tables AS (SELECT self.node_id AS table_id FROM doc AS node_table WHERE node_table.tag = 'table'), r_cells AS (SELECT r_table.table_id, TEXT(node_td) AS cell_text FROM r_tables AS r_table CROSS JOIN LATERAL (SELECT self FROM doc AS node_tr WHERE node_tr.parent_id = r_table.table_id AND node_tr.tag = 'tr') AS node_tr CROSS JOIN LATERAL (SELECT self FROM doc AS node_td WHERE node_td.parent_id = node_tr.node_id AND node_td.tag IN ('th','td')) AS node_td) SELECT r_cells.cell_text FROM r_cells ORDER BY cell_text LIMIT 3;" \
  --input docs/fixtures/basic.html
```

## Before/after diagrams

```text
Before
  large query -> unclear failure source
```

```text
After
  A map rows
  B gate rows
  C extract one value
  D scale to full schema
```

## Common mistakes
- Skipping row inspection and starting with full extraction.  
  Fix: begin with `SELECT * ... LIMIT ...`.
- Changing multiple parts of a query at once.  
  Fix: adjust one checkpoint at a time and verify.

## Chapter takeaway
The first query loop is not training wheels; it is the fastest path to reliable extraction on unfamiliar HTML.
