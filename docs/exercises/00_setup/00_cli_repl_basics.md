# 00: CLI + REPL Basics

Where this module fits in the pipeline: this is the entry point before row selection. You learn how to run MarkQL once, then interactively.

## Story Context

You just received an HTML snapshot from a teammate. Before you can reason about query quality, you must be able to run commands quickly, inspect rows, and export output.

## Mission

Finish this module when you can:

- run one-off queries from CLI
- explore incrementally in REPL
- export a CSV without guessing commands

## Start MarkQL (single query)

```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query "SELECT section(node_id, tag) FROM doc WHERE tag = 'section' ORDER BY node_id LIMIT 2;"
```

## Start REPL

```bash
./build/markql --interactive
```

Inside REPL, load a local fixture:

```text
markql> .load docs/exercises/01_basics_select_where/fixtures/page.html
```

Then run a query:

```sql
SELECT section(node_id, tag)
FROM doc
WHERE attributes.data-kind = 'flight'
ORDER BY node_id;
```

REPL also accepts a pasted multi-statement block when statements are separated by `;`.

Exit REPL:

```text
markql> .quit
```

## Run a query file

```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query-file docs/exercises/01_basics_select_where/tasks/01_solution.sql
```

## Export CSV

```bash
./build/markql \
  --input docs/exercises/01_basics_select_where/fixtures/page.html \
  --query "SELECT section(node_id, tag) FROM doc WHERE tag = 'section' ORDER BY node_id TO CSV('/tmp/sections.csv');"
```

## Troubleshooting quick list

- Empty output
  - Check input path first.
  - Run a probe: `SELECT div(node_id, tag) FROM doc LIMIT 5;`
- Too many rows
  - Add stable row filters in outer `WHERE`.
  - Add `ORDER BY node_id LIMIT n` while debugging.
- Common syntax errors
  - Missing `FROM doc`
  - Missing commas in projection lists
  - Unbalanced parentheses
- REPL not exiting
  - Use `.quit` or `.q`

## Decision checkpoint

1. When debugging, should you start with full extraction or a small row probe?
2. If result is empty, what do you verify first?
3. What command exits REPL safely?

Answers:

1. Start with a small row probe.
2. Verify input path/source first.
3. `.quit` (or `.q`).

## What you can do now

- run MarkQL from CLI and REPL
- load local HTML and execute small probe queries
- export CSV for downstream checks
