# Chapter 2: Mental Model

## What is the MarkQL mental model?
The MarkQL mental model is: “A query is a two-stage computation over a DOM row stream.” Stage 1 filters which nodes survive as output rows. Stage 2 computes values for each surviving row using scoped field expressions. This two-stage model is the semantic center of the language.

This matters because it prevents scope confusion. In many query systems, one expression simultaneously picks rows and values, which hides evaluation order. MarkQL keeps those concerns separated. You can reason about row inclusion first, then reason about value sourcing. That separation reduces accidental data loss and makes null behavior understandable rather than mysterious.

This may feel unfamiliar at first if you are used to one-shot selector APIs. It is common to expect `TEXT(span WHERE ...)` to filter rows directly. In MarkQL it does not; it only picks a supplier node for one field on a row that already exists. That distinction feels strict at first and then becomes liberating.

> ### Note: “Two-stage evaluation” is MarkQL’s core concept
> MarkQL teaches “which stage owns decision-making at each point.”
> - Stage 1 owns row existence.
> - Stage 2 owns field values.
> Mixing those responsibilities in your mental model creates almost every confusing result.

## Rules
- Stage 1: outer `WHERE` filters row candidates.
- Stage 2: field expressions run once per kept row.
- Field-level predicates inside `TEXT/ATTR` pick suppliers, not rows.
- `NULL` field values do not remove rows.
- Use `EXISTS(...)` in outer `WHERE` when supplier existence should affect row inclusion.

## Scope
Primary scope names used in this book:
- **Row node**: current node in outer scan
- **Axis node(s)**: nodes reachable via `parent/child/ancestor/descendant`
- **Supplier node**: node chosen by a field expression for one output field

```text
Outer loop:
  doc rows -> row node R
            -> outer WHERE decides keep/drop
            -> if keep: evaluate fields for R
```

```text
Inside a field:
  supplier search space = R (+ scoped descendants/axes)
  choose first/last/indexed match
  return value or NULL
```

## Listing 2-1: Observe stage 1 only
Query:

```sql
SELECT section.node_id
FROM doc
WHERE tag = 'section'
  AND EXISTS(child WHERE tag = 'h3')
ORDER BY node_id;
```

<!-- VERIFY: ch02-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(child WHERE tag = 'h3') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6},
  {"node_id":11},
  {"node_id":16}
]
```

This listing deliberately avoids field extraction to isolate stage 1. The output tells you only which rows survive. When debugging, that isolation is powerful: first make row inclusion correct, then move to fields.

## Listing 2-2: Add stage 2 explicitly
Query:

```sql
SELECT section.node_id,
PROJECT(section) AS (
  title: TEXT(h3),
  stop_text: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')
)
FROM doc
WHERE tag = 'section'
ORDER BY node_id;
```

<!-- VERIFY: ch02-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')) FROM doc WHERE tag = 'section' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","stop_text":"1 stop"},
  {"node_id":11,"title":"Osaka","stop_text":"nonstop"},
  {"node_id":16,"title":"Kyoto Stay","stop_text":null}
]
```

Notice row `16` remains even though `stop_text` is null. This is the two-stage model in one line of output: stage 1 kept the row, stage 2 could not find a supplier for that one field.

## Listing 2-3: Deliberate failure (invalid axis)
Naive query:

```sql
SELECT section.node_id FROM doc WHERE EXISTS(foo WHERE tag = 'h3');
```

<!-- VERIFY: ch02-listing-3-fail -->
```bash
# EXPECT_FAIL: Expected axis name
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE EXISTS(foo WHERE tag = 'h3');" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: Query parse error: Expected axis name (self, parent, child, ancestor, descendant)
```

The parser is protecting the scope model. `EXISTS` must declare an axis universe. A vague axis name would make evaluation ambiguous.

## Listing 2-4: Correct axis-based filter

<!-- VERIFY: ch02-listing-4 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE tag='section' AND EXISTS(descendant WHERE tag='span' AND attributes.role = 'text') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6},
  {"node_id":11}
]
```

## Before/after scope diagrams

```text
Before (common confusion)
  field predicate controls row survival
```

```text
After (actual semantics)
  outer WHERE -> row survival
  field WHERE -> supplier choice
```

Keep this chapter’s model active as you read the rest of the book. It is not one chapter’s concept; it is the language’s operating system.
