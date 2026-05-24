---
name: markql-extract
description: Extract structured data from an HTML file using MarkQL queries without loading raw HTML into LLM context. Use when the user asks to scrape, query, or extract info from an HTML file/URL, or mentions MarkQL extraction. Drives a structure-probe loop (probe → draft → run → check → refine) and produces output in a user-chosen format (table, CSV, JSON, NDJSON, Parquet).
---

# markql-extract

Extract data from HTML using the `markql` CLI. Never pipe raw HTML into the conversation — work through structural probes and bounded queries.

## Non-negotiable rules

1. **Never `cat`, `Read`, or otherwise dump the HTML file into context.** Even on "small" files, prefer probes.
2. **Never `SELECT inner_html(...)` or `TEXT(body)` etc. without a tight `WHERE` and `LIMIT`.** Those dump page content.
3. **Always confirm the desired output format** with the user before final extraction: `TABLE` (stdout), `CSV`, `JSON`, `NDJSON`, `PARQUET`. Default to `JSON` only if the user is silent.
4. **Restate the goal in one sentence** before probing: what rows, what fields per row.

## Quick start

```bash
markql --query "SELECT COUNT(*) FROM doc;" --input page.html
markql --query "SELECT COUNT(*) FROM doc WHERE tag = 'div';" --input page.html
markql --query "SELECT n.node_id, n.tag, ATTR(n, class) FROM doc AS n WHERE n.tag = 'section' LIMIT 5;" --input page.html
```

## Workflow loop

1. **Restate the goal** (rows + fields).
2. **Confirm output format** (table / csv / json / ndjson / parquet + path).
3. **Cheap first signal — `html_inspector --families-compact`** (when available in the project):
   ```bash
   ./tools/html_inspector/target/release/inspect --families-compact <file>
   ```
   Output is one line per detected repeated family: `<id>|<parent>><child>|<count>|<D/H/U>|<F/P>|slot:<supplier>|sig:<shape>`.
   - **If** the family you want is listed, use its `parent>child` as your row anchor and `slot:` as the supplier hint. Skip ahead to step 8.
   - **Validated limits**: compact mode misses `<dl>/<dt>` repetitions and can miss large class-less `<ul>/<li>` lists buried deep. If your goal data isn't visible, do **not** escalate to `--families` (verbose dump — burns tokens on large pages). Fall through to step 4.
4. **Anchor probe (markql fallback)** — find one durable reference point near the target:
   ```sql
   SELECT COUNT(*) FROM doc WHERE DIRECT_TEXT(self) LIKE '%<anchor text>%';
   SELECT n.node_id, n.tag, ATTR(n, class), ATTR(n, id) FROM doc AS n
     WHERE DIRECT_TEXT(self) LIKE '%<anchor text>%' LIMIT 5;
   ```
5. **Walk to the container**. From the anchor node, get `parent_id`, then list siblings/children to find the row container:
   ```sql
   SELECT n.node_id, n.tag, n.parent_id, ATTR(n, class) FROM doc AS n WHERE n.node_id = <id>;
   SELECT n.node_id, n.tag, ATTR(n, class) FROM doc AS n WHERE n.parent_id = <id> LIMIT 30;
   ```
6. **Count expected rows** to confirm the container is right:
   ```sql
   SELECT COUNT(*) FROM doc WHERE tag = '<row_tag>' AND parent.node_id = <container_id>;
   ```
7. **Probe row internals** — list the descendants' tags + classes to discover field suppliers:
   ```sql
   SELECT n.node_id, n.tag, ATTR(n, class) FROM doc AS n WHERE ancestor.node_id = <one_row_id> LIMIT 30;
   ```
8. **Sample one supplier** to confirm what its `TEXT()` actually contains. Inside `PROJECT`, `TEXT(<tag>)` returns only direct text — verify the supplier value matches what you expect before scaling.
9. **Draft** the `PROJECT` query. Use **structural predicates** in field expressions, not blind positional indices, when rows can have status variants. Keep helper-row CTEs separate from `PROJECT`/`FLATTEN` shaped CTEs — do **not** mix row-id helper columns with shaped extraction in the same CTE (see `tools/html_inspector/docs/ai_markql_musts.md` rule 8).
10. **Lint first** (fast feedback, no execution): `markql --lint "<query>" --format json`.
11. **Run with `LIMIT 5`**, inspect, then widen.
12. **Diagnose `NULL`s in place** — supplier probe on the failing field, never dump the page. Read NULLs as signal: all rows wrong = row-gate issue; correct rows + one NULL field = supplier issue; mixed = variant rows.
13. **Export** in the chosen format.
14. **Report** row count + output path. Do not paste data inline unless the user asked and it's small (~<20 rows).

## Output forms

```sql
-- stdout
SELECT ... FROM doc WHERE ...;
SELECT ... FROM doc WHERE ... TO JSON();
SELECT ... FROM doc WHERE ... TO NDJSON();

-- file
SELECT ... FROM doc WHERE ... TO CSV('out.csv');
SELECT ... FROM doc WHERE ... TO JSON('out.json');
SELECT ... FROM doc WHERE ... TO NDJSON('out.ndjson');
SELECT ... FROM doc WHERE ... TO PARQUET('out.parquet');

-- HTML <table> extraction
SELECT table FROM doc WHERE id = '<id>' TO TABLE(EXPORT='out.csv');
```

For repeated runs against the same file:
```bash
markql --input page.html --write-mqd cache/page.mqd
markql --query-file q.sql --input cache/page.mqd
```

## Syntax gotchas (validated against the live CLI)

These will fail. Use the right form on the right side.

| ✗ Does NOT work | ✓ Use instead |
|---|---|
| `SELECT tag, COUNT(*) FROM doc GROUP BY tag` | `GROUP BY` is unsupported. Run per-tag `SELECT COUNT(*) FROM doc WHERE tag = '<t>'`. |
| `SELECT n.attr.class FROM doc AS n` | `SELECT n.node_id, ATTR(n, class) FROM doc AS n` |
| `SELECT div(node_id, attributes.class) FROM doc` | Tag-tuple takes only **core fields** (`node_id`, `tag`, `parent_id`, `sibling_pos`, `max_depth`, `doc_order`). For attrs: alias form + `ATTR(n, <name>)`. |
| `SELECT self(...) FROM doc` | `SELECT n.node_id, n.tag FROM doc AS n` |
| `ORDER BY n.node_id` | `ORDER BY node_id` (bare field; restricted set only: `node_id`, `tag`, `text`, `parent_id`, `sibling_pos`, `max_depth`, `doc_order`) |
| `PROJECT(n) AS (cls: ATTR(self, class))` | `ATTR()` inside `PROJECT` needs a tag id, not `self`. Use `ATTR(<tag>, <name> WHERE ...)`. |
| `DIRECT_TEXT(my_alias)` returns nothing when `DIRECT_TEXT(self)` matches | Prefer `DIRECT_TEXT(self)` or `DIRECT_TEXT(<tag>)` in WHERE clauses on aliased rows. |
| `SELECT TEXT(div) FROM doc;` | `TEXT()/INNER_HTML()` require a non-tag self-predicate in `WHERE`. |
| `WHERE NOT (expr LIKE '%...%')` or `WHERE x NOT LIKE '%...%'` | `NOT (...)` and `NOT LIKE` are unsupported. Invert by positive match (`= 'X'`, `LIKE '%anchor%'`) or by `<>` / `IS NULL`. |
| `WHERE ... = (SELECT ...)` | Subqueries inside scalar predicates are unsupported. Look up the value with a separate probe, then inline it. |
| `PROJECT(n) AS (loc: TEXT(p))` returns only the *direct* text node | Inside `PROJECT`, `TEXT(<tag>)` is supplier-scoped and stops at the tag's direct text — nested element content is dropped. Drill into the descendant that holds the value (e.g. `TEXT(span WHERE parent.tag = 'p')`). Outside `PROJECT`, plain `TEXT(n)` aggregates as usual. |
| Mixing row-id helper columns with `PROJECT(...)`/`FLATTEN(...)` in the same CTE | Keep two CTE roles strictly separate: (a) row-anchor helpers carrying `node_id`/scalars, (b) pure shaped relations via `PROJECT`/`FLATTEN`. Mixed CTEs lose row identity and produce misleading NULLs. |

## Token-discipline cheatsheet

- DO: `COUNT(*)`, alias-form probes with `LIMIT ≤ 5`, `--lint`, supplier probes.
- DON'T: `Read` the HTML file, paste raw `inner_html`, `SELECT *` without `LIMIT`, dump hundreds of rows to stdout.
- If the user pastes raw HTML into chat, write it to a temp file first, then switch to probes.

## Refining when results are wrong

| Symptom | Next probe |
|---|---|
| 0 rows | Drop the strictest predicate, count again. |
| Too many rows | Add `EXISTS(descendant WHERE ...)` for required structure. |
| Column all `NULL` | Supplier probe: `SELECT TEXT(<tag>) FROM doc WHERE <supplier predicate> LIMIT 3;` |
| Field shifts on `FIRST_TEXT` / positional `TEXT(t, N)` | Variant rows present — replace positional with `WHERE` predicate on direct text or attributes. |
| Column drift on `FLATTEN` | Switch to explicit `PROJECT(...) AS (...)`. |
| Parse error | `markql --lint "<q>" --format json` — read `why` / `help` / `example`. |

## Self-discovery

```sql
SHOW FUNCTIONS;
SHOW AXES;
SHOW OPERATORS;
DESCRIBE doc;
DESCRIBE language;
```

## Reference

Read on demand, not eagerly:
- `REFERENCE.md` (bundled) — 13 CLI-verified query templates. Open this **first** when drafting any non-trivial query.
- `tools/html_inspector/docs/ai_markql_musts.md` — strict agent contract for MarkQL drafting (CTE rules, escalation order, anti-patterns). Authoritative when in conflict with anything here.
- `tools/html_inspector/docs/ai_inspection_playbook.md` — four-step low-token workflow combining `html_inspector` and MarkQL.
- `tools/html_inspector/docs/compact_family_mode.md` — how to read `--families-compact` output.
- `docs/markql-cli-guide.md` — CLI flags, output modes, axes, operators, PROJECT semantics.
- `docs/markql-tutorial.md` — row/field mental model, troubleshooting checklist.

CLI exit codes: `0` success, `1` parse/runtime error, `2` CLI/IO usage error.
