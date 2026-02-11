# Chapter 10: NULL and Stability

## What is NULL stability in MarkQL?
NULL stability is the practice of designing field extraction so missing suppliers are expected, controlled, and documented rather than treated as random failures. In MarkQL, nulls are a normal part of stage-2 evaluation.

This matters because real-world HTML is inconsistent. Optional badges, missing subtitles, absent price spans, and layout variants are common. Stable extraction systems do not pretend those cases do not exist; they encode fallback behavior explicitly.

This can feel unfamiliar if you expect every column to be non-null by default. In extraction languages, null is often the honest value. The key is what you do next: preserve it, coalesce it, or use it to gate rows with outer `WHERE`.

> ### Note: Null in stage 2 is different from “row dropped” in stage 1
> If outer `WHERE` removes a row, the row disappears entirely. If a stage-2 field cannot find a supplier, the row remains and that one field is null. Treating those as the same event is a major source of confusion.

## Rules
- Use `COALESCE` for explicit fallback defaults.
- Use outer `EXISTS` if missing supplier should remove row.
- Keep null-producing fields visible while debugging.
- Prefer deterministic normalization (`TRIM`, `LOWER`, `REPLACE`) before fallback when needed.

## Scope

```text
Stage 1 outcome: keep row R
Stage 2 for field F:
  supplier exists? yes -> value
                  no  -> NULL
```

```text
Policy layer:
  raw field     -> NULL allowed
  final field   -> COALESCE(raw, default)
```

## Listing 10-1: Null-aware projection

<!-- VERIFY: ch10-listing-1 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), subtitle: TEXT(p WHERE attributes.class = 'summary'), subtitle_final: COALESCE(TEXT(p WHERE attributes.class = 'summary'), 'N/A')) FROM doc WHERE tag = 'section' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"subtitle":null,"subtitle_final":"N/A"},
  {"node_id":11,"subtitle":null,"subtitle_final":"N/A"},
  {"node_id":16,"subtitle":"Near station","subtitle_final":"Near station"}
]
```

## Listing 10-2: Conditional stability with `CASE`

<!-- VERIFY: ch10-listing-2 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TRIM(TEXT(h3)), kind: CASE WHEN attributes.data-kind = 'flight' THEN 'transport' ELSE 'other' END) FROM doc WHERE tag='section' ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","kind":"transport"},
  {"node_id":11,"title":"Osaka","kind":"transport"},
  {"node_id":16,"title":"Kyoto Stay","kind":"other"}
]
```

## Listing 10-3: Deliberate failure (`CASE` missing END)

<!-- VERIFY: ch10-listing-3-fail -->
```bash
# EXPECT_FAIL: Expected END to close CASE expression
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (kind: CASE WHEN attributes.data-kind = 'flight' THEN 'transport') FROM doc WHERE tag='section';" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: Query parse error: Expected END to close CASE expression
```

Fix is straightforward: always close CASE blocks explicitly.

## Before/after diagrams

```text
Before
  missing supplier -> panic / ad-hoc patch
```

```text
After
  missing supplier -> NULL -> COALESCE/CASE policy
```
