# Chapter 5: Row Filtering with WHERE

## TL;DR
`WHERE` is the row gate. If row filtering is wrong, no extraction function can rescue the result.

## Using `self` (the current row node)
“`self` refers to the current node for the current row produced by FROM.”

Use `self` when you want to inspect the current row node directly without guessing a supplier tag.

```sql
SELECT self.node_id, self.tag
FROM doc
LIMIT 5;
```

```sql
SELECT self.node_id, self.tag, DIRECT_TEXT(self) AS dt
FROM doc
WHERE DIRECT_TEXT(self) LIKE '%some_substring%';
```

```sql
SELECT self.node_id, self.tag
FROM doc
WHERE EXISTS(descendant WHERE DIRECT_TEXT(self) LIKE '%some_substring%');
```

## What is `WHERE` in MarkQL?
`WHERE` is the stage-1 row filter. It runs on each candidate row from `FROM` and decides whether that row survives into the output. In MarkQL, this is the single place where row existence is decided.

This matters because many extraction mistakes are actually row-filter mistakes. If `WHERE` is too broad, you get noisy rows and confusing null fields. If `WHERE` is too narrow, you get empty output and assume extraction is broken. MarkQL’s model asks you to solve row scope first, then values.

This may feel unfamiliar if you expect field functions to implicitly control row inclusion. They do not. They can return null, but null does not remove a row. If row inclusion must depend on a structural fact, put that fact in outer `WHERE` (often via `EXISTS`).

> ### Note: `WHERE` runs over rows that already exist in the stream
> MarkQL does not run `WHERE` over arbitrary text fragments. It runs it over node rows with identity. That means `tag`, `attributes`, axis expressions, and operators are all evaluated in row context. This is why `WHERE` is explainable and debuggable with `LIMIT` and small projections.

## Rules
- Keep `WHERE` focused on row inclusion criteria.
- Use `IN` and `LIKE` for readable matching logic.
- Use `EXISTS(axis WHERE ...)` for structural requirements.
- Avoid encoding field sourcing logic directly into outer `WHERE` unless row inclusion depends on it.
- If a filter seems “text-like,” verify whether it should be `LIKE` or axis-based.

## Scope

```text
FOR each row R in doc:
  evaluate WHERE(R)
  if true -> keep R
  else    -> drop R
```

```text
R context has:
  R.tag
  R.attributes.*
  axis-relative nodes from R
```

## Listing 5-1: Stable row gate with attributes

<!-- VERIFY: ch05-listing-1 -->
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

The filter is intentionally explicit and readable. If `data-kind` changes, you know exactly why rows disappeared.

## Listing 5-2: `LIKE` with normalization

<!-- VERIFY: ch05-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, section.tag FROM doc WHERE attributes.data-kind IS NOT NULL AND LOWER(attributes.data-kind) LIKE '%flight%' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"tag":"section"},
  {"node_id":11,"tag":"section"}
]
```

This listing demonstrates a common pattern: normalize first (`LOWER`), match second (`LIKE`). It is simple but robust.

## Listing 5-3: Deliberate failure (`CONTAINS` misuse)
Naive query:

```sql
SELECT section.node_id FROM doc WHERE text CONTAINS 'stop';
```

<!-- VERIFY: ch05-listing-3-fail -->
```bash
# EXPECT_FAIL: CONTAINS supports only attributes
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE text CONTAINS 'stop';" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: CONTAINS supports only attributes
```

Why this fails: in the current implementation, `CONTAINS` is attribute-oriented. For textual wildcard matching in row filters, use `LIKE` over an expression that returns string data.

Fix example:

<!-- VERIFY: ch05-listing-4 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE attributes.data-kind IS NOT NULL AND LOWER(attributes.data-kind) LIKE '%flight%' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

## Before/after diagrams

```text
Before
  text CONTAINS 'x'  (assumed generic contains)
```

```text
After
  LOWER(attr) LIKE '%x%'  (supported and explicit)
```

## Common mistakes
- Using field logic to compensate for weak row filters.  
  Fix: make row inclusion criteria explicit first.
- Assuming all text-like checks should use `CONTAINS`.  
  Fix: use the operator forms supported by current implementation (`LIKE`, attribute `CONTAINS`, etc.).

## Chapter takeaway
Treat outer `WHERE` as your primary correctness boundary for every query.
