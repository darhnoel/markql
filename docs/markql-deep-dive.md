---
title: MarkQL Deep Dive
uuid: 6acc4092-068e-11f1-bdc1-5bce0c0d51c8
version: 22
created: '2026-02-10T23:40:16+09:00'
updated: '2026-02-11T00:32:00+09:00'
tags:
  - markql
  - xsql
---

## TL;DR mental model

```
PROJECT(base_tag) = chooses row candidates by tag (or all for PROJECT(document))
Outer WHERE       = filters those row candidates

PROJECT(...) field expressions run once per kept row

Field WHERE  = decides which row-scoped node supplies ONE column value
               (it does NOT decide whether the row exists)
```


---

## 1) Think in two nested loops (implementation model)

Your query:

```
SELECT
  li.node_id,
  PROJECT(li) AS (
    city:  TEXT(h3),
    stops: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop'),
    price: TEXT(span WHERE attributes.role = 'text')
  )
FROM doc
WHERE tag = 'li'
  AND EXISTS(descendant WHERE tag = 'h3')
  AND EXISTS(descendant WHERE attributes.role = 'text');
```

Execution model:

```
FOR EACH node N in doc:
  IF N.tag != 'li': continue        // from PROJECT(li)

  /* Outer WHERE: row filter */
  IF (EXISTS(...) AND EXISTS(...)):
      emit one output row for N
      and compute SELECT expressions (including PROJECT fields)

  ELSE:
      skip (no row)
```

Then inside each emitted row:

```
PROJECT(li):
  city  = evaluate TEXT(h3) in row scope (self + descendants)
  stops = evaluate TEXT(span WHERE ...) in row scope
  price = evaluate TEXT(span WHERE ...) in row scope
```

So it is literally:

```
doc-scan loop (rows)
  -> per-row projection loop (columns)
```


---

## 2) Scope diagram: two WHEREs live in different universes

### Universe A: row candidate + outer WHERE

`PROJECT(li)` picks candidate rows with tag `li`; outer `WHERE` filters those candidates.

```
[doc]
  ├─ node #1 tag=li    ── test outer WHERE ── keep? yes/no
  ├─ node #2 tag=li    ── test outer WHERE ── keep? yes/no
  └─ node #3 tag=div   ── skipped before WHERE
```

### Universe B: field WHERE (column extraction)

Inside `PROJECT(li)`, each field searches **row scope = row node + descendants**.

```
(row node)
  ├─ self node
  └─ descendants
      └─ pick ONE that matches field predicate
```

This is the single biggest point:

- Outer WHERE decides if a candidate row stays.
- Field WHERE decides which node supplies a field value.


---

## 3) Concrete DOM tree + matching

Let’s draw one `li` row node:

```
li (row_node)
├─ h3                  "Tokyo"
├─ span                "1 stop"
├─ span                "2 hr"
├─ span                "30 min"
└─ span role="text"    "$463"
```

Now evaluate each field:

### Field 1: `city: TEXT(h3)`

```
Candidates = row scope nodes with tag=h3
          = [h3("Tokyo")]

Pick a match (default: first in document order)
Return TEXT(h3) = "Tokyo"
```

### Field 2: `stops: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')`

```
Candidates = row scope nodes with tag=span
          = [span("1 stop"), span("2 hr"), span("30 min"), span("$463")]

Apply predicate: HAS_DIRECT_TEXT 'stop'
Matches   = [span("1 stop")]   (maybe also "nonstop" depending on rule)
Pick one  = span("1 stop")
Return    = "1 stop"
```

### Field 3: `price: TEXT(span WHERE attributes.role = 'text')`

```
Candidates = row scope nodes with tag=span
Matches   = [span(role="text", "$463")]
Return    = "$463"
```

If a field finds no match:

```
Matches = []
Return = NULL
```

No row removal occurs due to a NULL field.


---

## 4) Why same condition behaves differently

Consider two queries that look similar.

### A) Condition inside field selection (keeps all candidate rows)

```
SELECT li.node_id,
PROJECT(li) AS (
  stop_text: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')
)
FROM doc
WHERE tag='li';
```

Interpretation:

```
Row set = all nodes where tag=li

For each li:
  stop_text = (find matching span) OR NULL
```

Result shape:

```
li#1  stop_text="1 stop"
li#2  stop_text=NULL        <-- row stays
li#3  stop_text="nonstop"
```

### B) Condition moved to outer WHERE (filters rows)

```
SELECT li.node_id,
PROJECT(li) AS (
  stop_text: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')
)
FROM doc
WHERE tag='li'
  AND EXISTS(descendant WHERE tag='span' AND span HAS_DIRECT_TEXT 'stop');
```

Interpretation:

```
Row set = only li rows that contain such a span

For each kept li:
  stop_text = same extraction (now likely non-NULL)
```

Result shape:

```
li#1  stop_text="1 stop"
li#3  stop_text="nonstop"
(li#2 removed)
```

Same logical idea, different scope: one is extraction, one is row filtering.


---

## 5) `EXISTS(...)` mental model (outer WHERE helper)

This part is worth its own diagram because it’s how you “bridge” inner matching into row filtering.

When you write:

```
EXISTS(descendant WHERE tag='span' AND attributes.role='text')
```

It means:

```
Within the current row node (li),
scan its descendant nodes.
If ANY descendant satisfies the predicate -> TRUE
Else -> FALSE
```

ASCII:

```
li
└─ descendants: [d1, d2, d3, ...]
     predicate(d1)? no
     predicate(d2)? no
     predicate(d3)? yes  -> EXISTS = true (stop scanning)
```

Common use:
> Keep rows likely to have extractable values.

Important: this is a row filter helper, not a guarantee that every projected field is non-NULL.


---

## 6) End-to-end pipeline

This shows the whole query evaluation:

```
Step 1) Scan doc nodes
        N1, N2, N3, ... Nk

Step 2) Candidate filter from PROJECT(base_tag)
        - PROJECT(li) means only nodes with tag='li'

Step 3) Evaluate outer WHERE on candidate node
        - EXISTS(descendant WHERE tag='h3') ?
        - EXISTS(descendant WHERE role='text') ?
        If false -> discard node

Step 4) For each kept row, compute SELECT outputs
        - node_id (direct)
        - PROJECT(li) (per-row extraction)

Step 5) PROJECT(li) expands into multiple fields:
        For each field:
          a) find candidates in row scope (self + descendants) by tag/axis
          b) apply field predicate (optional)
          c) choose one match (default first; can use FIRST/LAST/nth)
          d) return TEXT/ATTR/etc (or NULL)

Step 6) Emit final table row
```


---

## 7) One-line explanation

If you want a line you can reuse in docs/tutorials:

> `PROJECT(base_tag)` picks row candidates, outer `WHERE` filters them, and field predicates choose the node that supplies each column.


---

## 8) Row boundary diagram

A good teaching trick is to draw a boundary box around the subtree:

```
doc
├─ li#1  ┌───────────────────────────────┐
│        │ h3 "Tokyo"                    │
│        │ span "1 stop"                 │
│        │ span role=text "$463"         │
│        └───────────────────────────────┘
├─ li#2  ┌───────────────────────────────┐
│        │ h3 "Osaka"                    │
│        │ span "promo"                  │
│        └───────────────────────────────┘
└─ footer ...
```

Then say:

- `PROJECT(li)` + outer `WHERE` decides whether `li#1`, `li#2` become rows.

- Inside each box, field predicates search row scope and pick one value.

---

## 9) Selector stability and field behavior (important)

- Default picker:
  - `TEXT(tag WHERE ...)` and `ATTR(tag, attr WHERE ...)` return the first matching node in row scope.
- Stable selection options:
  - `FIRST_TEXT(...)`, `LAST_TEXT(...)`, `TEXT(..., n)`
  - `FIRST_ATTR(...)`, `LAST_ATTR(...)`, `ATTR(..., n)`
- Out-of-range index returns `NULL`.

Example:

```sql
PROJECT(li) AS (
  first_stop: FIRST_TEXT(span WHERE span HAS_DIRECT_TEXT 'stop'),
  last_stop:  LAST_TEXT(span WHERE span HAS_DIRECT_TEXT 'stop'),
  second_hr:  TEXT(span WHERE span HAS_DIRECT_TEXT 'hr', 2)
)
```

---

## 10) `HAS_DIRECT_TEXT` and string matching

- `HAS_DIRECT_TEXT` is kept for compatibility.
- It is treated as shorthand for direct-text matching (`DIRECT_TEXT(...) LIKE '%...%'` style behavior).
- Current LIKE/contains matching in execution is case-insensitive.

---

## 11) Alias references and `COALESCE` in `PROJECT`

- `PROJECT` fields are evaluated left-to-right.
- A later field can reference an earlier alias.
- `COALESCE(...)` returns the first non-NULL, non-blank value in this path.

Example:

```sql
PROJECT(li) AS (
  raw_price:  TEXT(span WHERE attributes.role = 'text'),
  price_trim: TRIM(raw_price),
  price:      COALESCE(price_trim, 'N/A')
)
```
