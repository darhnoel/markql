# Chapter 8: FLATTEN

## TL;DR
`FLATTEN` is the fastest way to explore hierarchical text as columns, but it is not a long-term schema guarantee.

## What is `FLATTEN`?
`FLATTEN(tag[, depth])` is an extraction shortcut that maps ordered descendant text slices into output columns. It is designed for quick structure discovery and early prototyping when strict per-column supplier logic is not yet needed.

It matters because early exploration often needs speed over precision. You may not yet know the best supplier predicate per field. `FLATTEN` lets you quickly inspect text ordering and draft a schema. Then, once drift is visible, you can migrate to `PROJECT` with explicit fields.

This can feel unfamiliar because `FLATTEN` succeeds even when data shape varies per row, which can produce column drift. That behavior is not a bug; it is the tradeoff. `FLATTEN` optimizes discovery. `PROJECT` optimizes stability.

> ### Note: `FLATTEN` is a prototyping tool, not a final contract
> If your source rows can omit fields or reorder chunks, flattened columns can shift. Use flatten output to *learn* field positions quickly, then codify business columns in `PROJECT` with supplier predicates and fallback logic.

## Rules
- Use `FLATTEN` first when you are mapping unknown DOM structure.
- Expect drift when rows have optional nodes.
- Keep alias list explicit with `AS (...)` for readability.
- Move to `PROJECT` for production schema.
- Keep row set constrained before flattening.

## Scope

```text
kept row R
  gather ordered text slices from subtree(R)
  assign slices left-to-right to aliases
```

```text
if row has fewer slices
  trailing aliases become NULL
if row has different slice meaning
  semantic drift appears
```

## Listing 8-1: Fast flatten mapping
Fixture: `docs/fixtures/products.html`

<!-- VERIFY: ch08-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, FLATTEN(li) AS (name, note, lang1, lang2) FROM doc WHERE tag = 'li' ORDER BY node_id;" \
  --input docs/fixtures/products.html
```

Observed output:

```json
[
  {"node_id":3,"name":"Alpha","note":"Fast and light","lang1":"EN","lang2":"JP"},
  {"node_id":8,"name":"Beta","note":"EN","lang1":null,"lang2":null},
  {"node_id":11,"name":"Gamma","note":"Budget","lang1":"EN","lang2":"TH"}
]
```

The second row demonstrates drift: the `note` column became language text because `p` was absent.

## Listing 8-2: Deliberate failure boundary with aggregate mixing

<!-- VERIFY: ch08-listing-2-fail -->
```bash
# EXPECT_FAIL: Aggregate queries require a single select item
./build/markql --mode plain --color=disabled \
  --query "SELECT COUNT(*), FLATTEN(li) AS (a) FROM doc WHERE tag='li';" \
  --input docs/fixtures/products.html
```

Observed error:

```text
Error: Aggregate queries require a single select item
```

This is useful because it shows a parser/validator boundary: aggregate shape rules are enforced before execution.

## Listing 8-3: Stable rewrite with PROJECT

<!-- VERIFY: ch08-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, PROJECT(li) AS (name: TEXT(h2), note: COALESCE(TEXT(p), 'n/a'), lang_primary: FIRST_TEXT(span), lang_secondary: TEXT(span, 2)) FROM doc WHERE tag='li' ORDER BY node_id;" \
  --input docs/fixtures/products.html
```

Observed output:

```json
[
  {"node_id":3,"name":"Alpha","note":"Fast and light","lang_primary":"EN","lang_secondary":"JP"},
  {"node_id":8,"name":"Beta","note":"n/a","lang_primary":"EN","lang_secondary":null},
  {"node_id":11,"name":"Gamma","note":"Budget","lang_primary":"EN","lang_secondary":"TH"}
]
```

## Before/after diagrams

```text
Before
  FLATTEN -> quick columns, possible drift
```

```text
After
  PROJECT -> explicit suppliers, stable semantics
```

## Common mistakes
- Treating flatten column positions as permanent schema contracts.  
  Fix: migrate to `PROJECT` once exploration is done.
- Flattening overly broad row sets.  
  Fix: constrain rows before flattening to reduce noise.

## Chapter takeaway
Use `FLATTEN` to discover structure quickly, then encode production schema with `PROJECT`.
