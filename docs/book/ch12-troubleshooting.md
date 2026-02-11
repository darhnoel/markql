# Chapter 12: Troubleshooting

## What is troubleshooting in MarkQL?
Troubleshooting in MarkQL is a staged diagnosis process: isolate parse errors, isolate row-filter errors, isolate supplier-selection errors, and then validate output sink constraints. Because MarkQL is explicit, error messages usually point to stage boundaries.

This matters because most difficult extraction incidents are not random. They are usually one of a small number of recurring mistakes: grammar mismatch, scope confusion, unsupported sort key, or sink misuse. A disciplined troubleshooting flow turns those incidents from panic into checklists.

This can feel unfamiliar if you are used to silently failing selector APIs. MarkQL tends to fail loudly with explicit parser/validation messages. That loudness is useful once you map each error family to a stage of evaluation.

> ### Note: Error messages are stage hints
> - Parse errors hint grammar boundary issues.
> - Validation/runtime errors hint semantic constraints.
> - Empty rows hint row-scope mismatch.
> Treat error messages as signposts for where to inspect next, not just as strings to suppress.

## Rules
- Reproduce with smallest fixture and smallest query.
- Verify row scope before field scope.
- Keep one deliberate failure query per incident for future regression notes.
- Record exact command and exact observed error.
- Use `SHOW FUNCTIONS`, `SHOW AXES`, `SHOW OPERATORS` to confirm capabilities.

## Scope

```text
Troubleshooting ladder
  parser -> row stage -> field stage -> sink stage
```

```text
If row stage fails:
  fix WHERE / EXISTS / axis logic first
If field stage fails:
  fix TEXT/ATTR supplier logic
```

## Listing 12-1: Parse boundary failure (`Expected FROM`)

<!-- VERIFY: ch12-listing-1-fail -->
```bash
# EXPECT_FAIL: Expected FROM
./build/markql --mode plain --color=disabled \
  --query "select div(data-id) as data_id from doc;" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: Query parse error: Expected FROM
```

## Listing 12-2: Semantic boundary failure (`ORDER BY` expression)

<!-- VERIFY: ch12-listing-2-fail -->
```bash
# EXPECT_FAIL: Unexpected token after query
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id FROM doc WHERE tag='section' ORDER BY attributes.data-kind;" \
  --input docs/fixtures/basic.html
```

Observed error:

```text
Error: Query parse error: Unexpected token after query
```

In this build, `ORDER BY` supports core row fields. Sorting by expression-like paths is not supported yet.

## Listing 12-3: Capability introspection

<!-- VERIFY: ch12-listing-3 -->
```bash
./build/markql --mode plain --color=disabled --query "SHOW FUNCTIONS;"
```

<!-- VERIFY: ch12-listing-4 -->
```bash
./build/markql --mode plain --color=disabled --query "SHOW AXES;"
```

<!-- VERIFY: ch12-listing-5 -->
```bash
./build/markql --mode plain --color=disabled --query "SHOW OPERATORS;"
```

Observed outputs (trimmed):
- `SHOW FUNCTIONS` includes `project(tag)`, `flatten(tag[, depth])`, `coalesce`, `case`.
- `SHOW AXES` includes `parent`, `child`, `ancestor`, `descendant`.
- `SHOW OPERATORS` includes `LIKE`, `IN`, `IS NULL`, `HAS_DIRECT_TEXT`, and logical operators.

## Listing 12-4: Corrective rewrite pattern

<!-- VERIFY: ch12-listing-6 -->
```bash
./build/markql --mode plain --color=disabled \
  --query "SELECT section.node_id, PROJECT(section) AS (title: TEXT(h3), stop_text: TEXT(span WHERE span HAS_DIRECT_TEXT 'stop')) FROM doc WHERE tag='section' AND EXISTS(descendant WHERE tag='span' AND text LIKE '%stop%') ORDER BY node_id;" \
  --input docs/fixtures/basic.html
```

Observed output:

```json
[
  {"node_id":6,"title":"Tokyo","stop_text":"1 stop"},
  {"node_id":11,"title":"Osaka","stop_text":"nonstop"}
]
```

## Before/after diagrams

```text
Before
  unclear error -> random query edits
```

```text
After
  identify stage -> apply stage-specific fix -> verify
```
