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
  --query-file docs/case-studies/queries/github_contributions_calendar.sql
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

## Traditional Scraping (BeautifulSoup)

```python
from bs4 import BeautifulSoup
import json

with open("github_profile.html", "r", encoding="utf-8") as f:
    soup = BeautifulSoup(f, "html.parser")

WEEKDAYS = [
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday",
]

def normalize_day(parent_text: str) -> str:
    t = " ".join(parent_text.split())
    for d in WEEKDAYS:
        if t.startswith(d):
            return d
    return t

rows = []
for td in soup.find_all("td", id=lambda v: v and "contribution-day-component" in v):
    parent_text = td.parent.get_text(" ", strip=True) if td.parent else ""
    rows.append({
        "data_ix": td.get("data-ix"),
        "data_date": td.get("data-date"),
        "data_level": td.get("data-level"),
        "day": normalize_day(parent_text),
    })

print(json.dumps(rows, indent=2, ensure_ascii=False))
```

## MarkQL Equivalent

```sql
SELECT PROJECT(td) AS (
  data_ix: ATTR(td, data-ix),
  data_date: ATTR(td, data-date),
  data_level: ATTR(td, data-level),
  day: CASE
    WHEN TRIM(parent.text) LIKE 'Sunday%' THEN 'Sunday'
    WHEN TRIM(parent.text) LIKE 'Monday%' THEN 'Monday'
    WHEN TRIM(parent.text) LIKE 'Tuesday%' THEN 'Tuesday'
    WHEN TRIM(parent.text) LIKE 'Wednesday%' THEN 'Wednesday'
    WHEN TRIM(parent.text) LIKE 'Thursday%' THEN 'Thursday'
    WHEN TRIM(parent.text) LIKE 'Friday%' THEN 'Friday'
    WHEN TRIM(parent.text) LIKE 'Saturday%' THEN 'Saturday'
    ELSE TRIM(parent.text)
  END
)
FROM doc
WHERE tag = 'td'
  AND id CONTAINS 'contribution-day-component';
```

Both versions enforce the same core logic:

- Row filter: contribution `td` nodes only.
- Field extraction: `data_ix`, `data_date`, `data_level`.
- Day normalization: map noisy parent text to canonical weekday names.
