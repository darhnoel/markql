# GitHub Contributions Calendar

## Problem

GitHub contribution cells expose stable numeric/date fields on each `td`, but weekday labels are rendered as nearby text and can include noisy multiline variants such as `Sunday ... Sun`.

Goal: return one row per contribution cell with a canonical weekday string.

## Query Source

- `docs/case-studies/queries/github_contributions_calendar.sql`

## Run

```bash
./build/markql \
  --input <your-github-profile-snapshot.html> \
  --query "$(tr '\n' ' ' < docs/case-studies/queries/github_contributions_calendar.sql)"
```

## Invariants

- `Row Invariant`: `WHERE tag = 'td' AND id CONTAINS 'contribution-day-component'` must select only contribution cells.
- `Field Invariant`: `data_ix`, `data_date`, and `data_level` are read directly from stable `data-*` attributes on the selected row.
- `Normalization Invariant`: `day` is derived from `TRIM(parent.text)` and normalized by prefix matching (`LIKE 'Sunday%'`, etc.) so short-label noise does not leak into output.

## Failure Modes

- `Mixed Row Scope`: if non-contribution `td` nodes enter scope, attribute fields may become `NULL` or semantically wrong.
- `Unnormalized Day`: using raw `parent.text` directly can produce multiline values (`Sunday\n...\nSun`) instead of canonical weekdays.
- `Over-specific Label Parse`: parsing exact full text instead of prefix-based weekday checks is brittle across UI label formatting changes.

## Output Contract

Each output row represents exactly one contribution cell and has:

- `data_ix`: contribution index within the calendar grid.
- `data_date`: ISO date string, for example `2025-02-09`.
- `data_level`: contribution intensity level.
- `day`: one of `Sunday`, `Monday`, `Tuesday`, `Wednesday`, `Thursday`, `Friday`, `Saturday`.

Example row:

```json
{
  "data_ix": "0",
  "data_date": "2025-02-09",
  "data_level": "0",
  "day": "Sunday"
}
```
