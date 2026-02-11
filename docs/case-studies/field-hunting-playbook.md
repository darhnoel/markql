# Field Hunting Playbook

This guide teaches a repeatable way to extract stable data from unfamiliar HTML.

The scenario is a “gold offer board”: each card represents one supplier offer, but the page includes incomplete promo cards and repeated sub-blocks. If you extract too early, you get noisy rows and NULL-heavy output. If you hunt rows first and fields second, extraction becomes predictable.

## What Is Field Hunting?

Field hunting is the process of finding **stable row anchors** first, then attaching each output column to a specific supplier node within each row.

Why this matters: most scraping breakage happens because extraction starts from convenient selectors rather than durable structure. MarkQL gives you a way to make extraction explicit: first decide *which nodes become rows* (outer `WHERE`), then decide *how each column is computed* (`PROJECT(...)` field expressions).

This may feel unfamiliar at first if you are used to “just grab text from the page.” That feeling is normal. After a few runs, this two-stage flow becomes the fastest way to debug.

## Note: Read This Mental Model First

> ### Note
> MarkQL does not “query a page string.” It queries a **row stream of DOM nodes**.
>
> - Stage 1 picks row nodes: `FROM doc WHERE ...`
> - Stage 2 computes columns per row: `PROJECT(row) AS (...)`
>
> If Stage 1 is wrong, Stage 2 cannot save you.
>
> Think of it like ore processing:
> - Stage 1: choose the right ore carts
> - Stage 2: refine metal from each cart
>
> Refining the wrong cart still gives the wrong output.

## Rules You Should Keep in Mind

1. Never start with a big `PROJECT(...)`; inspect row candidates first.
2. Prefer durable anchors over cosmetic selectors.
3. Add `EXISTS(...)` guards before adding complicated field logic.
4. Build one field at a time and run after each change.
5. Treat repeated blocks explicitly with `FIRST_...` / `LAST_...`.
6. Normalize string formats in-query so downstream tools stay simple.

## Scope: What Is “In Scope” At Each Step?

```text
doc
└── many nodes
    └── node matched by outer WHERE   <-- current row node
        ├── descendant used by field A
        ├── descendant used by field B
        └── descendant used by field C
```

Outer `WHERE` sees the whole document stream one row at a time.

Inside `PROJECT(row)`, each field resolves suppliers relative to that row. This is why one row can produce complete values while another row returns NULL: they are evaluated independently.

## Listing 1: Probe Row Candidates Before Extraction

The first query should answer: “What cards am I actually selecting?”

```bash
./build/markql --input docs/case-studies/fixtures/gold_offers_sample.html --query "SELECT div(node_id, tag, max_depth) FROM doc WHERE attributes.data-testid = 'offer';"
```

Observed output:

```text
node_id  tag  max_depth
3        div  2
16       div  2
29       div  2
Rows: 3
```

Interpretation:

- You have three candidate cards.
- Row shape is consistent (`tag=div`, `max_depth=2`), which is a good sign.
- You still do not know whether every row has all required fields.

This is exactly where many extraction attempts fail: they skip this probe and jump to field extraction.

## Anchor Strategy: Choose What Is Expensive To Change

Use this priority, top to bottom:

1. Structural/business anchors (`data-*`, semantic ids, durable product keys)
2. Domain-specific URL/path patterns (`src`, `href`, known route structure)
3. Styling classes (supporting constraints, not primary identity)

Notes:

- `data-testid`, `data-qa`, and `role` are examples, not mandatory names.
- `role` alone is usually too broad.
- Class names are often generated or design-driven; assume they drift more often.

The practical question is not “can I match this node today?” but “how likely is this signal to survive normal UI edits?”

## Listing 2: A Naive Query That Looks Correct But Is Not Stable

The query below looks reasonable, but it does not enforce quote completeness:

```bash
./build/markql --input docs/case-studies/fixtures/gold_offers_sample.html --query "SELECT div.node_id, PROJECT(div) AS ( vendor: FIRST_ATTR(img, alt WHERE attributes.src CONTAINS '/vendors/small/'), quote_text: TEXT(span WHERE attributes.class CONTAINS 'price-main') ) FROM doc WHERE attributes.data-testid = 'offer';"
```

Observed output:

```text
node_id  vendor                 quote_text
3        Aurum Guild            ¥72,400
16       Terra Prospect         ¥64,980
29       Placeholder Mining Co  NULL
Rows: 3
```

Why this fails:

- The third row matches the row anchor (`data-testid='offer'`).
- But it is a promotional variant with no `price-main`.
- Stage 1 admitted a row that Stage 2 cannot fully populate.

Fix direction:

- Keep the row anchor.
- Add structural quality guards with `EXISTS(...)`.

## Listing 3: Add Structural Guards To Remove Incomplete Rows

Now add “must-have” descendants before extracting everything:

```sql
AND EXISTS(descendant WHERE attributes.class CONTAINS 'ore-grade')
AND EXISTS(descendant WHERE attributes.class CONTAINS 'price-main')
```

This keeps cards that are real offers and drops teaser variants.

## Listing 4: Final Query With Stable Fields And Normalized Price

Query file:

- `docs/case-studies/queries/gold_offers_sample.sql`

Run:

```bash
./build/markql --input docs/case-studies/fixtures/gold_offers_sample.html --query "$(tr '\n' ' ' < docs/case-studies/queries/gold_offers_sample.sql)"
```

Observed output (trimmed):

```text
node_id  vendor          primary_grade  reserve_grade  ...  quote_text  quote_jpy
3        Aurum Guild     Grade A Vein   Reserve B Vein ...  ¥72,400     72400
16       Terra Prospect  Grade B Vein   Reserve C Vein ...  ¥64,980     64980
Rows: 2
```

What this query gets right:

- Rows are filtered for completeness first.
- Repeated blocks are explicit (`FIRST_TEXT`/`LAST_TEXT`).
- Currency text and machine-friendly numeric form are both emitted.

Normalization pattern used:

```sql
TRIM(REPLACE(REPLACE(REPLACE(price_text, '¥', ''), '￥', ''), ',', ''))
```

This keeps downstream analytics clean and avoids repeating cleaning logic in every consumer.

## Common Failure Patterns

### 1. All columns become NULL

Cause:

- row filter is too broad, or suppliers are scoped too loosely

Fix:

1. rerun a row-only probe (`node_id`, `tag`, `max_depth`)
2. tighten row `WHERE`
3. test each field expression independently

### 2. One row is malformed while others look good

Cause:

- mixed variants inside the same row anchor family

Fix:

- add `EXISTS(...)` guards for required building blocks
- avoid temporary `node_id <> ...` as a permanent solution

### 3. One field contains merged content

Cause:

- supplier selected too high in the subtree

Fix:

- choose a narrower supplier node
- avoid broad “container text” when you need atomic values

## Practical Checklist

1. Row anchor chosen by durability, not convenience
2. Row count validated before field extraction
3. `EXISTS(...)` guards added for mandatory blocks
4. `PROJECT(...)` fields added incrementally
5. Repeated blocks mapped with `FIRST_`/`LAST_`
6. Price/text normalized in-query
7. Query re-tested on a fresh capture

If you follow this sequence, field hunting stops feeling like trial-and-error and becomes a controlled, debuggable process.
