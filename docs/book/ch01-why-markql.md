# MarkQL in One Chapter
*From brittle scraping scripts to readable, reproducible extraction.*

## TL;DR
MarkQL lets you query HTML as rows instead of manually walking DOM trees in code. You write what data you want, and MarkQL handles traversal and extraction rules in one place. Start by finding stable rows, then compute columns from those rows.

If this feels different from your usual scraping workflow, that is expected. Most people first learn scraping as a sequence of imperative steps. MarkQL asks you to express intent first: row criteria, then field criteria. After a few runs, that shift usually makes query behavior easier to predict.

## The pain: why scraping breaks
If you have done scraping in Python, JavaScript, or any browser automation stack, you already know the failure pattern:
- the selector worked yesterday,
- frontend changed one wrapper `<div>` or class,
- your extraction returns empty strings or wrong rows,
- then you spend an hour stepping through traversal code and patching conditionals.

The painful part is not just “DOM changed.” The painful part is where your logic lives.
A lot of scraping code mixes three jobs together:
1. finding candidate blocks,
2. traversing to nested nodes,
3. shaping output schema.

When those three jobs are spread across imperative glue code, every small page change can trigger a large debugging session. You are not just fixing one selector; you are re-checking loops, index assumptions, optional branches, and post-processing rules.

That is why scraping feels fragile: your extraction intent is buried inside orchestration code.

A practical way to say this: when extraction logic is spread across traversal code, every selector change forces you to re-read control flow. When extraction logic is centralized, selector changes usually affect one query clause rather than a full pipeline.

## The paradigm shift: “How” vs “What”
Traditional scraping often looks like this:
- Declarative selectors (`.select(...)`, XPath, CSS)
- Imperative orchestration (`for` loops, `if` branches, fallback logic, mutation)

MarkQL shifts the center of gravity:
- Keep extraction declarative as long as possible
- Put row filtering and field computation directly in the query
- Keep traversal intent visible in one place

In plain words:
- **Old style**: “How do I walk this tree and build rows?”
- **MarkQL style**: “What rows do I want, and what columns do I compute for each row?”

This can feel unfamiliar in the first few queries, especially if you are used to writing loops first and extraction second. That is normal. Once you separate row selection from field computation, the query becomes easier to read, review, and fix.

That one shift reduces cognitive load. You can review and debug extraction as a query contract instead of reverse-engineering control flow.

The core tradeoff is simple and useful: you give up some ad-hoc flexibility in exchange for clearer semantics. In scraping work, that trade is often worth it because maintainability and reproducibility matter more than quick one-off hacks.

## Side-by-side comparison
### Traditional extraction code
```python
cards = soup.select("section")
rows = []
for card in cards:
    if card.get("data-kind") != "flight":
        continue

    h3 = card.select_one("h3")
    spans = card.select("span")

    stop_text = None
    price = None
    for sp in spans:
        t = sp.get_text(" ", strip=True)
        if "stop" in t.lower():
            stop_text = t
        if sp.get("role") == "text":
            price = t

    rows.append({
        "title": h3.get_text(strip=True) if h3 else None,
        "stops": stop_text,
        "price": price,
    })
```

### MarkQL query
```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  stops: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%'),
  price: TEXT(span WHERE attributes.role = 'text')
)
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

The important difference is not fewer lines. The important difference is clarity:
- row inclusion is explicit (`WHERE attributes.data-kind = 'flight'`)
- each output field has explicit supplier logic
- output shape is declared where extraction happens

That clarity becomes concrete when you debug. If output rows are wrong, inspect outer `WHERE`. If columns are wrong, inspect field expressions in `PROJECT`. The query itself separates those concerns, so the debugging path is shorter and less ambiguous.

## The killer feature: stable schema extraction with `PROJECT`
The fastest “aha” in MarkQL is `PROJECT(...)`.

`PROJECT` says:
- “For each row I keep, compute these named fields.”

That sounds small, but it solves a big problem: schema drift.
In exploratory extraction, `FLATTEN` is great and fast. But when rows differ (optional nodes, missing blocks), flattened columns can shift meaning. `PROJECT` lets you pin each column to a field expression, so your schema stays understandable.

You can still use `FLATTEN` for discovery. But for extraction you want to maintain, `PROJECT` is usually the bridge from “it works on my sample” to “it keeps working next month.”

In other words, `PROJECT` makes the extraction contract explicit. Each field is named, each value source is documented by expression, and optional data can be handled with deliberate defaults. That explicitness is what makes handoff and review easier across a team.

## Beginner mental model
Think of a page as a table of nodes.

1. `FROM doc` gives you rows (nodes).
2. `WHERE` keeps rows you care about.
3. `PROJECT` (or fields) computes columns for each kept row.

```text
HTML page
   |
   v
[table of nodes]
   |
   |  WHERE (keep rows)
   v
[rows you care about]
   |
   |  PROJECT/TEXT/ATTR (compute columns)
   v
[result table]
```

Light teaser for what comes next in the book:
- Outer `WHERE` decides if a row exists.
- Field expressions decide what value each column gets for that row.

That two-step split is the core of reliable MarkQL queries.

> ### Note: one row, then many field decisions
> A useful picture is: “one row enters, several field expressions run.”  
> The row is either kept or dropped once. Field expressions then compute values independently for that kept row.  
> This is why one field can be `NULL` while other fields in the same row are valid.

## Try it in 60 seconds
Use fixture: `docs/fixtures/basic.html`

### a) Show me the rows
Query idea: inspect the first 5 nodes so you can see actual structure.

<!-- VERIFY: ch01-try-01 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT * FROM doc LIMIT 5;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {
    "attributes": {},
    "doc_order": 0,
    "max_depth": 5,
    "node_id": 0,
    "parent_id": null,
    "tag": "html"
  },
  {
    "attributes": {},
    "doc_order": 1,
    "max_depth": 4,
    "node_id": 1,
    "parent_id": 0,
    "tag": "body"
  },
  {
    "attributes": {"id": "content"},
    "doc_order": 2,
    "max_depth": 3,
    "node_id": 2,
    "parent_id": 1,
    "tag": "main"
  },
  {
    "attributes": {"id": "nav"},
    "doc_order": 3,
    "max_depth": 1,
    "node_id": 3,
    "parent_id": 2,
    "tag": "nav"
  },
  {
    "attributes": {"href": "/home", "rel": "nav"},
    "doc_order": 4,
    "max_depth": 0,
    "node_id": 4,
    "parent_id": 3,
    "tag": "a"
  }
]
```

This command is your structural sanity check. Before choosing any extraction strategy, confirm what the parser actually produced: tags, ids, parent relations, and document order. Most downstream mistakes get cheaper to fix if this step is done first.

### b) Extract something real
Query idea: keep only flight sections and compute title, stops, and price.

<!-- VERIFY: ch01-try-02 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stops: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%'), price: TEXT(span WHERE attributes.role = 'text')) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {
    "node_id": 6,
    "price": "¥12,300",
    "stops": "1 stop",
    "title": "Tokyo"
  },
  {
    "node_id": 11,
    "price": "¥8,500",
    "stops": "nonstop",
    "title": "Osaka"
  }
]
```

This output shows a complete extraction contract in a small form:
- row gate: only sections with `data-kind='flight'`
- field logic: `title`, `stops`, `price`
- stable shape: each row has the same named columns

As chapters progress, syntax becomes richer, but this flow does not change.

## Common beginner mistakes
### Mistake 1: jumping straight to extraction without stable rows
Bad query:
```sql
SELECT TEXT(section) FROM doc;
```
What goes wrong:
- fails with extraction guard (`requires a WHERE clause`), or
- if rewritten loosely, you still don’t know which rows you meant.

Fix:
```sql
SELECT TEXT(section)
FROM doc
WHERE attributes.data-kind = 'flight';
```
Start with explicit row scope first.

### Mistake 2: mixing row filtering with field selection
Bad query pattern:
```sql
SELECT section.node_id,
PROJECT(section) AS (
  stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')
)
FROM doc
WHERE tag = 'section';
```
Then expecting only stop rows.

What goes wrong:
- rows without matching supplier stay in output with `stop_text = NULL`.

Fix (if row must have stop text):
```sql
SELECT section.node_id,
PROJECT(section) AS (
  stop_text: TEXT(span WHERE DIRECT_TEXT(span) LIKE '%stop%')
)
FROM doc
WHERE tag = 'section'
  AND EXISTS(descendant WHERE tag = 'span' AND text LIKE '%stop%');
```
Outer `WHERE` controls row existence. Field expressions control value sourcing.

Why this matters operationally: if your pipeline expects “only rows with stop text,” stage-1 filtering must encode that requirement. If you leave it in field selection only, you get silent null rows rather than explicit exclusion.

### Mistake 3: trying to build schema with ad-hoc flattening too early
Bad pattern:
- rely on positional flattened columns before understanding optional nodes.

What goes wrong:
- columns drift when one row is missing a node.

Fix:
- use `FLATTEN` for quick discovery,
- switch to `PROJECT` for stable named columns.

## Closing
Good extraction is not about writing clever selectors. It is about making intent obvious and repeatable: which rows, which fields, which fallbacks.

MarkQL gives you a path from “I can scrape this page today” to “we can maintain this extractor next quarter.”

**Sticky line:** first lock the rows, then compute the columns. That one habit will save you the most debugging time in MarkQL.
