# Chapter 4: Sources and Loading

## TL;DR
Source choice controls reproducibility. Use stable sources while developing queries, then switch inputs deliberately for production workflows.

## What are MarkQL sources?
A MarkQL source is the input root that supplies the row stream. `doc` is the canonical parsed input source in CLI runs, but MarkQL also supports file/URL string sources, `RAW(...)` inline HTML, and `FRAGMENTS(...)` when you need multiple top-level roots.

This matters because source choice affects reproducibility. A query that works on captured local HTML is reproducible for tests. A query that reads from network input may change over time. MarkQL supports both, but you should choose deliberately based on whether you are debugging, testing, or running production extraction.

This may feel unfamiliar if you normally tie scraping logic to browser state directly. In MarkQL, source and query are separate concerns. That separation is practical: you can freeze one HTML fixture and iterate on query semantics quickly.

> ### Note: Source is where determinism starts
> Teams often think determinism starts in query syntax. It starts earlier, at input. If the source changes every run, debugging semantic issues becomes noisy. MarkQL’s source system (`--input`, `RAW`, `FRAGMENTS`, stdin) is intentionally explicit so you can control that noise.

## Rules
- Use `--input <file>` for reproducible local runs.
- Use `doc` as the default source table for the loaded input.
- Use `RAW(...)` for tiny inline fixtures in docs/tests.
- Use `FRAGMENTS(...)` when your snippet has sibling roots.
- Use stdin when piping dynamic HTML from another command.

## Scope

```text
CLI input path
  --input file.html
      -> parsed DOM
      -> available as table: doc
```

```text
RAW/FRAGMENTS
  query literal source
      -> parsed in-query
      -> row stream local to that source expression
```

## Listing 4-1: File source via `--input`

<!-- VERIFY: ch04-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE tag='section' ORDER BY node_id;" \
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

## Listing 4-2: Stdin source

<!-- VERIFY: ch04-listing-2 -->
```bash
printf '<div class="x">stdin</div>' | \
./build/markql --mode plain --color=disabled \
  --query "SELECT div FROM doc WHERE attributes.class = 'x';"
```

Observed output:

```json
[
  {"node_id":2,"tag":"div","attributes":{"class":"x"},...}
]
```

## Listing 4-3: Inline `RAW(...)`

<!-- VERIFY: ch04-listing-3 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT div FROM RAW('<div class=\"x\">hello</div>');"
```

Observed output (trimmed):

```json
[
  {"tag":"div","attributes":{"class":"x"},...}
]
```

## Listing 4-4: Deliberate failure (`TEXT` guard still applies)
Even with explicit source, extraction guard rules remain.

<!-- VERIFY: ch04-listing-4-fail -->
```bash
# EXPECT_FAIL: requires a WHERE clause
./build/markql --mode plain --color=disabled \
  --query "SELECT TEXT(div) FROM RAW('<div>hello</div>');"
```

Observed error:

```text
Error: TEXT()/INNER_HTML()/RAW_INNER_HTML() requires a WHERE clause
```

Fix:

<!-- VERIFY: ch04-listing-5 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT TEXT(div) FROM RAW('<div class=\"x\">hello</div>') WHERE attributes.class = 'x';"
```

Observed output:

```json
[
  {"text":"hello"}
]
```

## Listing 4-5: `FRAGMENTS(...)` for multiple roots

<!-- VERIFY: ch04-listing-6 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT div FROM FRAGMENTS(RAW('<div id=\"a\">one</div><div id=\"b\">two</div>')) ORDER BY node_id;"
```

Observed output:

```json
[
  {"attributes":{"id":"a"},...},
  {"attributes":{"id":"b"},...}
]
```

## Before/after diagrams

```text
Before
  query correctness depends on live page timing
```

```text
After
  freeze source -> iterate query -> verify outputs
```

## Common mistakes
- Debugging query semantics against constantly changing live HTML.  
  Fix: reproduce with local fixtures or `RAW(...)`.
- Forgetting that extraction guard rules still apply with `RAW(...)`.  
  Fix: keep explicit row narrowing in outer `WHERE`.

## Chapter takeaway
Good extraction starts before query syntax: choose input sources that make behavior repeatable.
