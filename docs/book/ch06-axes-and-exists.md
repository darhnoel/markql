# Chapter 6: Axes and EXISTS

## What are axes and `EXISTS`?
Axes (`parent`, `child`, `ancestor`, `descendant`) define structural traversal relative to the current row. `EXISTS(axis WHERE ...)` asks whether at least one node in that axis scope satisfies a predicate.

This matters because structural invariants survive class churn better than cosmetic attributes. If a row “must have a heading and a price node,” that is a structural claim. MarkQL lets you encode that claim directly instead of writing brittle selector chains.

This may feel unfamiliar if you have mostly used selector APIs that hide traversal decisions. Axes make traversal visible. That visibility adds a little syntax, but it buys explicit control and debuggability.

> ### Note: Axes are scoped to the current row
> In outer `WHERE`, the current row is each candidate node from stage 1. So `EXISTS(descendant WHERE ...)` means descendants of *that row*, not global descendants of the whole document. This scoping rule explains why the same EXISTS clause can be true for one row and false for another in the same query.

## Rules
- Use `EXISTS` when row inclusion depends on presence/absence of structure.
- Read axis predicates as sentences: “this row has a descendant where ...”.
- Prefer `descendant` for broad checks, `child` for strict depth.
- Keep axis predicates small and composable.
- Validate axis assumptions with tiny row projections before full extraction.

## Scope

```text
row R
  parent scope     -> at most 1 node
  child scope      -> direct children of R
  ancestor scope   -> chain up from R
  descendant scope -> all nodes below R
```

```text
EXISTS(axis WHERE P)
  true  if any node in axis scope satisfies P
  false otherwise
```

## Listing 6-1: Child existence gate

<!-- VERIFY: ch06-listing-1 -->
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

## Listing 6-2: Descendant existence gate with predicate

<!-- VERIFY: ch06-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE tag = 'section' AND EXISTS(descendant WHERE tag = 'span' AND attributes.role = 'text') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6},
  {"node_id":11}
]
```

Row `16` is excluded because it does not have a matching descendant span with `role='text'`.

## Listing 6-3: Deliberate failure (invalid axis symbol)

<!-- VERIFY: ch06-listing-3-fail -->
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

Fix by choosing an explicit axis (`child` or `descendant`) based on depth requirements.

## Before/after diagrams

```text
Before
  EXISTS(unknown_scope WHERE ...)
```

```text
After
  EXISTS(descendant WHERE tag='span' AND ...)
```
