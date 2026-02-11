# Chapter 7: Value Extraction with TEXT and ATTR

## TL;DR
Extraction functions answer “which value do I take from this kept row?” They do not decide whether a row exists.

## What are `TEXT`, `DIRECT_TEXT`, and `ATTR`?
These are stage-2 value extraction functions. `TEXT(tag ...)` returns text from a selected supplier node, `DIRECT_TEXT(tag)` returns immediate text children only, and `ATTR(tag, name ...)` returns an attribute value from a selected supplier node.

They matter because DOM extraction is not just row selection. You must define *where each value comes from*. MarkQL forces this explicitness so schemas stay understandable. Instead of implicit “best guess” extraction, each field expresses supplier constraints.

This may feel unfamiliar because there is a guard: certain extraction functions require a narrowing `WHERE` in the query. That guard is intentional. It prevents accidental whole-document extraction and pushes users toward explicit row scope.

> ### Note: Supplier node selection is separate from row selection
> When you call `TEXT(h3)` inside `PROJECT(section)`, the query is not searching the whole document. It searches supplier nodes relative to the current row scope. If no supplier matches, the field returns `NULL` and the row still exists. This is one of the most important distinctions in MarkQL.

## Rules
- Use `TEXT` when you want descendant text from supplier nodes.
- Use `DIRECT_TEXT` when descendant text pollution is a risk.
- Use `ATTR` for stable machine-readable values.
- Expect null when no supplier matches.
- Use `COALESCE` for optional fields.

## Scope

```text
row R kept by outer WHERE
  field expression picks supplier S under R
  value = function(S)
```

```text
if no S exists:
  value = NULL
  row remains in result
```

## Listing 7-1: Deliberate failure (guard)

<!-- VERIFY: ch07-listing-1-fail -->
```bash
# EXPECT_FAIL: requires a WHERE clause
./build/markql --mode plain --color=disabled \
  --query "SELECT TEXT(section) FROM doc;" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: TEXT()/INNER_HTML()/RAW_INNER_HTML() requires a WHERE clause
```

The guard reminds you to define row scope before extraction.

## Listing 7-2: Correct `TEXT` extraction with narrowing

<!-- VERIFY: ch07-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT TEXT(section) FROM doc WHERE attributes.data-kind = 'flight' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output (trimmed):

```json
[
  {"text":"...1 stop...¥12,300..."},
  {"text":"...nonstop...¥8,500..."}
]
```

## Listing 7-3: `ATTR` extraction

<!-- VERIFY: ch07-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT ATTR(a, href) FROM doc WHERE attributes.rel = 'nav' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"attr":"/home"},
  {"attr":"/about"}
]
```

## Listing 7-4: `DIRECT_TEXT` behavior

<!-- VERIFY: ch07-listing-4 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT DIRECT_TEXT(span) FROM doc WHERE attributes.class = 'stop' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"direct_text":"1 stop"},
  {"direct_text":"nonstop"}
]
```

## Before/after diagrams

```text
Before
  implicit extraction from unknown scope
```

```text
After
  row R fixed -> supplier S selected -> value extracted
```

## Common mistakes
- Treating extraction failures as row-filter failures.  
  Fix: debug row scope and supplier scope separately.
- Ignoring `DIRECT_TEXT` when nested text pollutes matches.  
  Fix: use `DIRECT_TEXT` for immediate-text conditions.

## Chapter takeaway
Reliable extraction comes from explicit supplier logic, not from hoping one selector fits every row variation.
