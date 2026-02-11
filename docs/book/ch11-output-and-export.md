# Chapter 11: Output and Export

## TL;DR
MarkQL result semantics stay the same across sinks; only serialization format changes. Pick the sink that matches your downstream workflow.

## What are output sinks in MarkQL?
Output sinks are query targets that serialize result rows: `TO LIST()`, `TO CSV(...)`, `TO JSON(...)`, and `TO NDJSON(...)`. They are not just convenience features; they define interoperability boundaries with downstream tools.

This matters because extraction value is realized downstream. If output shape and format are unstable, downstream systems become fragile. MarkQL keeps sink syntax explicit in the query so result shaping and export intent are reviewable together.

This may feel unfamiliar if you are used to handling output entirely in host language code. In MarkQL, sink intent can be encoded at query level, which reduces glue code and keeps extraction semantics close to serialization semantics.

> ### Note: Sink choice is part of contract design
> - `LIST` is scalar-focused.
> - `CSV` is tabular and easy for spreadsheets/SQL ingestion.
> - `JSON` is array-oriented payload.
> - `NDJSON` is stream-friendly and append-friendly.
> Choosing sink early clarifies column expectations.

## Rules
- Use `TO LIST()` only for one projected column.
- Use `TO CSV` for human-readable table exports.
- Use `TO JSON` for array payload integration.
- Use `TO NDJSON` for streaming pipelines.
- Verify sink constraints with small test exports first.

## Scope

```text
query result rows
  -> sink serializer
  -> file or stdout
```

```text
same row semantics, different wire format
```

## Listing 11-1: JSON array to stdout

<!-- VERIFY: ch11-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, PROJECT(li) AS (name: TEXT(h2)) FROM doc WHERE tag = 'li' ORDER BY node_id TO JSON();" \
  --input docs/fixtures/products.html
```

Observed output:

```json
[{"node_id":"3","name":"Alpha"},{"node_id":"8","name":"Beta"},{"node_id":"11","name":"Gamma"}]
```

## Listing 11-2: NDJSON to stdout

<!-- VERIFY: ch11-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, PROJECT(li) AS (name: TEXT(h2), note: COALESCE(TEXT(p), 'n/a')) FROM doc WHERE tag = 'li' ORDER BY node_id TO NDJSON();" \
  --input docs/fixtures/products.html
```

Observed output:

```json
{"node_id":"3","name":"Alpha","note":"Fast and light"}
{"node_id":"8","name":"Beta","note":"n/a"}
{"node_id":"11","name":"Gamma","note":"Budget"}
```

## Listing 11-3: CSV to file

<!-- VERIFY: ch11-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT li.node_id, PROJECT(li) AS (name: TEXT(h2), note: COALESCE(TEXT(p), 'n/a')) FROM doc WHERE tag = 'li' ORDER BY node_id TO CSV('/tmp/markql_products.csv');" \
  --input docs/fixtures/products.html
```

Observed file `/tmp/markql_products.csv`:

```csv
node_id,name,note
3,Alpha,Fast and light
8,Beta,n/a
11,Gamma,Budget
```

## Listing 11-4: Deliberate failure (`TO LIST` shape)

<!-- VERIFY: ch11-listing-4-fail -->
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

Fix: use one projected value for LIST, or switch to a multi-column sink.

## Before/after diagrams

```text
Before
  extract -> custom serializer script
```

```text
After
  extract + sink in one query contract
```

## Common mistakes
- Choosing `TO LIST()` for multi-column output.  
  Fix: use `CSV`, `JSON`, or `NDJSON` for table-shaped results.
- Deferring sink choices until late pipeline stages.  
  Fix: declare sink intent early so shape assumptions stay explicit.

## Chapter takeaway
Output is part of the extraction contract, not an afterthought.
