# 99: Capstone - End-to-End Workflow

Where this module fits in the pipeline: this is the full pipeline in one exercise, from messy input to verified export.

## Scenario

You have a messy market page with real offers, teaser blocks, and mixed metal types.

## Mission

Produce a CSV that another team can trust without manual cleanup.

Goal:

1. keep only real gold offer rows
2. extract stable named columns
3. emit deterministic CSV
4. verify output against expected data

## Run the capstone solution

```bash
./build/markql \
  --input docs/exercises/99_capstone/fixtures/page.html \
  --query-file docs/exercises/99_capstone/solution.sql
```

## Verify the capstone output

```bash
docs/exercises/scripts/verify_exercises.sh
```

## Decision checkpoint

1. Which filters in this capstone are row-stability guards?
2. Which part converts rich nodes into machine-friendly columns?
3. Why is deterministic ordering required before verification?

Answers:

1. `data-testid`, `data-kind`, and `EXISTS(descendant ...)` checks.
2. `PROJECT(div) AS (...)`.
3. To avoid false diffs from non-deterministic row order.

## What you can do now

- implement a real extraction workflow from noisy HTML
- ship stable row filters and field extraction together
- verify output drift automatically before merging changes
