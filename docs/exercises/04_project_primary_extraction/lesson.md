# 04: PROJECT as Primary Extraction

Where this module fits in the pipeline: after stable row selection, PROJECT is the main mechanism for turning one row node into named columns.

## Story Context

Now row selection is stable. Product needs business-ready columns (`city`, `price_text`, `stop_text`) that remain understandable as markup changes.

## Mission

By the end of this module, you can ship readable, stable extraction queries with clear separation between row gating and field suppliers.

## Goal

Master the separation of concerns:

- outer `WHERE` selects rows
- `PROJECT(...)` conditions extract field suppliers

## Task list

- `tasks/01_task.md` - minimal PROJECT extraction
- `tasks/02_task.md` - add stop and price fields
- `tasks/03_task.md` - repeated values with FIRST/LAST selectors
- `tasks/04_task.md` - null-safe output with COALESCE
- `tasks/05_task.md` - CASE in projected fields
- `tasks/06_task.md` - anti-example 1: FLATTEN duplication, PROJECT fix
- `tasks/07_task.md` - anti-example 2: FLATTEN context loss, PROJECT fix
- `tasks/08_task.md` - anti-example 3: awkward FLATTEN selectors, PROJECT fix
- `tasks/09_task.md` - conversion drill: rewrite FLATTEN to PROJECT

## FLATTEN fails here, PROJECT fixes it (mini anti-examples)

1. Duplication
- FLATTEN output duplicates row identity when each row has different repetition size.
- PROJECT keeps one row per card with explicit fields.

2. Context loss
- FLATTEN positional columns (`v1`, `v2`, ...) hide semantics.
- PROJECT columns keep meaning (`city`, `price_text`, `stop_text`).

3. Awkward selectors
- FLATTEN needs rigid positional assumptions.
- PROJECT allows field-specific WHERE predicates per column.

## Decision checkpoint

1. What decides row inclusion in a PROJECT query?
2. Where do field supplier conditions belong?
3. If a projected field has no match, does the row disappear?

Answers:

1. Outer `WHERE`.
2. Inside `PROJECT(...)` field expressions.
3. No, field becomes `NULL`.

## What you can do now

- build stable named-column extraction queries
- keep row filtering and field extraction logically separate
- replace brittle positional transforms with semantic columns
