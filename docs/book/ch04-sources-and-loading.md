# Chapter 4: Sources and Loading

## TL;DR
Source choice controls reproducibility. Use stable sources while developing queries, then switch inputs deliberately for production workflows.

## What are MarkQL sources?
A MarkQL source is the input root that supplies the row stream. `doc` is the canonical parsed input source in CLI runs, but MarkQL also supports file/URL string sources, `RAW(...)` inline HTML, and `PARSE(...)` when you need to parse HTML strings into a source.

This matters because source choice affects reproducibility. A query that works on captured local HTML is reproducible for tests. A query that reads from network input may change over time. MarkQL supports both, but you should choose deliberately based on whether you are debugging, testing, or running production extraction.

This may feel unfamiliar if you normally tie scraping logic to browser state directly. In MarkQL, source and query are separate concerns. That separation is practical: you can freeze one HTML fixture and iterate on query semantics quickly.

> ### Note: Source is where determinism starts
> Teams often think determinism starts in query syntax. It starts earlier, at input. If the source changes every run, debugging semantic issues becomes noisy. MarkQL’s source system (`--input`, `RAW`, `PARSE`, stdin) is intentionally explicit so you can control that noise.

## Rules
- Use `--input <file>` for reproducible local runs.
- Use `doc` as the default source table for the loaded input.
- Use `RAW(...)` for tiny inline fixtures in docs/tests.
- Use `PARSE(...)` when your snippet has sibling roots or when HTML comes from query output.
- Use stdin when piping dynamic HTML from another command.
- Use `.mqd` when you want a stable parsed-document snapshot for repeated CLI runs.
- Use `.mqp` when you want a stable prepared query for repeated CLI runs.
- Use `.mql.j2` only with explicit `--render j2` when you want template-driven query files.

## Scope

```text
CLI input path
  --input file.html
      -> parsed DOM
      -> available as table: doc
```

```text
Artifact path
  --input file.mqd
      -> load parsed DOM snapshot
      -> available as table: doc

  --query-file file.mqp
      -> load prepared query
      -> execute against html/stdin/url/mqd input

  --query-file file.mql.j2 --render j2 --vars file.toml
      -> render plain MarkQL text first
      -> then lint/execute that rendered query
```

```text
RAW/PARSE
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

## Listing 4-5: `PARSE(...)` for multiple roots

<!-- VERIFY: ch04-listing-6 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT div FROM PARSE('<div id=\"a\">one</div><div id=\"b\">two</div>') AS frag ORDER BY node_id;"
```

Observed output:

```json
[
  {"attributes":{"id":"a"},...},
  {"attributes":{"id":"b"},...}
]
```

Compatibility note:
- `FRAGMENTS(...)` is still supported but deprecated.
- Migration: `FRAGMENTS(x)` -> `PARSE(x)`.

## Template query files via `--query-file`

`--query-file` can also load a templated query file when you opt in explicitly:

```bash
./build/markql \
  --query-file tests/fixtures/render/generic_query.mql.j2 \
  --render j2 \
  --vars tests/fixtures/render/generic_query.toml \
  --rendered-out /tmp/generic_query.mql \
  --lint
```

This keeps the boundary explicit:
- MarkQL does not change its grammar or semantics for templates.
- Jinja2 rendering happens first.
- The rendered output is plain MarkQL text.
- `--lint` then validates that rendered text exactly as if it came from a normal `.mql` file.

Recommended file naming:
- `query.mql.j2` for the template
- `query.toml` for vars
- `query.mql` for rendered output

## Versioned Artifacts

Experimental status:

- `.mqd` / `.mqp` artifact support is still experimental in this branch.
- Treat the feature as WIP even though the files are versioned and validated.

MarkQL's artifact MVP adds two explicit cacheable boundaries:

- `.mqd` stores the parsed node-table facts needed for execution
- `.mqp` stores a prepared query after parse + validate

This keeps the semantic boundary stable:

- MarkQL still executes against the same `doc` node-table model
- result behavior stays the same as direct HTML + SQL execution
- artifacts are versioned and rejected cleanly when major versions are incompatible
- artifacts are treated as untrusted data and validated before use
- the outer MarkQL artifact envelope stays custom while both `.mqd` `DOCN` and `.mqp` `QAST` payloads now use FlatBuffers
- older manual-`QAST` `.mqp` files still read through a narrow fallback when the FlatBuffers required-feature bit is absent

Create and inspect them from the CLI:

```bash
./build/markql --input docs/fixtures/basic.html --write-mqd /tmp/basic.mqd
./build/markql --query "SELECT a.href FROM doc WHERE href IS NOT NULL" --write-mqp /tmp/links.mqp
./build/markql --artifact-info /tmp/basic.mqd
```

Run a prepared query against a prepared document:

```bash
./build/markql --query-file /tmp/links.mqp --input /tmp/basic.mqd
```

MVP limits:

- artifact loading is only exposed through CLI `--input` / `--query-file`
- `--lint` still works on SQL text only
- `--lint --query-file query.mql.j2 --render j2 --vars query.toml` lints rendered SQL text
- `.mqp` is single-statement only
- compression is not used in the MVP artifact format

Security contract:

- `.mqd` and `.mqp` are untrusted files, not trusted serialized objects.
- The reader parses fields one-by-one instead of deserializing private C++ structs.
- Every textual field in the current format is strict UTF-8; malformed text is rejected.
- The reader enforces hard bounds on file size, section count, section size, string bytes, node count, attribute count, and collection counts before allocation.
- Payload checksum verification detects corruption/tampering before section payloads are trusted.
- `.mqd` loading is verified in two steps:
  - validate the MarkQL header, section framing, required features, and checksum
  - then verify the `DOCN` FlatBuffers payload before reading any document fields
- `.mqp` loading is verified in the same two steps, with the verifier applied to the `QAST` FlatBuffers payload before decoding prepared-query fields
- `--artifact-info` escapes control characters before printing artifact-derived metadata.
- The checksum is for corruption detection only; it is not an authenticity or signing mechanism.

Prepared-query semantic boundary:

- `.mqp` persists the validated `Query` AST meaning needed for later execution.
- It does not persist executor-private state or raw memory layouts.
- Query kind and source kind metadata must still match the decoded query after load.

Build note:

- The repo now carries a `vcpkg.json` manifest entry for FlatBuffers.
- `scripts/build/build.sh` installs FlatBuffers into `./vcpkg_installed` and uses the repo-local `flatc` during CMake generation.
- The build does not require system-wide FlatBuffers packages.
- `scripts/build/build.sh` auto-selects a default `vcpkg` triplet for Linux, macOS, and Windows shell environments, and you can override `VCPKG_TARGET_TRIPLET` / `VCPKG_HOST_TRIPLET` explicitly for cross-target builds.

Benchmark methodology and current result:

- `tests/bench_artifacts.cpp` runs 31 iterations and reports median values for query parse, query prepare, `.mqp` write, `.mqp` load, query-text execution on raw HTML, `.mqp` execution on raw HTML, `.mqp` execution on `.mqd`, and `.mqp` file size.
- On `examples/html/koku_tk.html`, the current medians were `0.060 ms` parse, `0.074 ms` prepare, `0.090 ms` `.mqp` write, `0.146 ms` `.mqp` load, `9.608 ms` query text on raw HTML, `9.642 ms` `.mqp` on raw HTML, `2.987 ms` `.mqp` on `.mqd`, with `1079` artifact bytes.
- That means this branch makes prepared-query load cheap, but on this fixture `.mqp` alone does not materially change raw-HTML execution time because HTML parsing still dominates. The larger win appears when `.mqp` is combined with `.mqd`.

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
Good extraction starts before query syntax: choose input sources that make behavior repeatable, and freeze parsed inputs or prepared queries when repeated-work cost matters more than one-off setup cost.
