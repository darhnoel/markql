# MarkQL Benchmark: Hockey Table Extraction

This benchmark answers: is MarkQL worth using for real HTML-to-CSV extraction versus mainstream Python stacks?

## Workload
Hero workload: extract a standings table (`table.hockey-stats`) into deterministic CSV across size variants.

Variants:
- `small`: 30 extracted rows.
- `medium`: 300 extracted rows.
- `big`: 3000 extracted rows.

All three keep the same schema and nested tags (`a`, `span`, `small`, `abbr`, `sup`) plus a `tr.totals` row that must be ignored.

Gold CSVs live in `bench/gold/` and are used as byte-for-byte correctness gates.

## Why this matters
MarkQL docs explicitly position `FLATTEN` for exploration and `PROJECT` for stability contracts, plus explicit sink contracts for deterministic export:
- `docs/book/ch08-flatten.md`
- `docs/book/ch09-project.md`
- `docs/book/ch10-null-and-stability.md`
- `docs/book/ch11-output-and-export.md`

This benchmark tests stability/correctness while scaling input size by two orders of magnitude.

## Engines compared
- MarkQL CLI (`build/markql`) using benchmark `.sql` queries.
- BeautifulSoup4 baseline (`bench/baselines/bs4_extract.py`) with parser: `lxml` (configurable).
- lxml baseline (`bench/baselines/lxml_extract.py`) using XPath traversal.

## Metrics
Per engine and per variant:
- End-to-end time (parse + extract + serialize)
- Parse-only time (when applicable)
- Extract-only time (when applicable)
- Peak RSS (`/usr/bin/time -v`, `Maximum resident set size`)
- Correctness (exact CSV match against gold)

Runner reports median and p90 over measured runs.

## Determinism rules
- Whitespace normalization: collapse internal whitespace, then trim.
- Missing cell policy: pad missing optional cells with blank fields.
- CSV contract: exact column order, byte-for-byte comparison with gold.
- Totals row is always excluded.

## MarkQL queries
- `bench/queries/hockey_table_to_table.sql`: documented `TO TABLE(...)` reference extraction.
- `bench/queries/hockey_table_to_csv.sql`: production CSV extraction for all size variants.

## Run
From repo root:

```bash
python3 bench/run.py --suite hockey
```

Python baseline dependencies:

```bash
python3 -m pip install beautifulsoup4 lxml
```

Common knobs:

```bash
python3 bench/run.py --suite hockey --warmups 2 --runs 9 --bs4-parser lxml
```

Machine-readable output:
- `bench/results/results.json`

## Interpreting "worth using"
In this benchmark, "worth using" means the engine is competitive on:
- performance (median/p90 latency),
- memory (peak RSS),
- correctness (exact gold match),
- stable correctness as input size scales from small to big.

MarkQL tradeoff to evaluate: query-level stability contract (`PROJECT` + sink semantics) versus host-language extractor flexibility and runtime overhead.

In this size-focused profile, a practical signal is how latency and RSS scale from `small` to `big` while maintaining byte-identical correctness.
