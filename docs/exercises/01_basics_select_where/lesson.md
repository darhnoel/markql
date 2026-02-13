# 01: Basics - SELECT + WHERE

Where this module fits in the pipeline: this is the first real row-selection step. You learn to get the correct rows before any advanced extraction.

## Story Context

You are extracting travel cards from a mixed page. Some nodes look similar, but only a subset are true flight results. If row selection is wrong here, every later extraction step is wrong.

## Mission

By the end of this module, you can confidently answer:

- "Which nodes are my real row units?"
- "Which filters are stable vs brittle?"
- "Is my output deterministic for verification?"

## Goal

Build a reliable habit:

1. Start with row probes (`node_id`, `tag`)
2. Add one filter at a time in outer `WHERE`
3. Keep output stable with `ORDER BY node_id`

## Task list

- `tasks/01_task.md` - first row probe
- `tasks/02_task.md` - filter by stable attributes
- `tasks/03_task.md` - add text condition safely
- `tasks/04_task.md` - control output size with `LIMIT`
- `tasks/05_task.md` - structural check with `EXISTS(child ...)`

## Decision checkpoint

1. Why add `ORDER BY node_id` in beginner exercises?
2. Is `WHERE` deciding row inclusion or field values?
3. If a field is missing, does that automatically remove the row?

Answers:

1. Deterministic output for debugging and verification.
2. Row inclusion.
3. No, missing field values can be `NULL` while row still exists.

## What you can do now

- select stable row sets with outer `WHERE`
- debug small result sets with predictable ordering
- use simple structural gates with `EXISTS(child ...)`
