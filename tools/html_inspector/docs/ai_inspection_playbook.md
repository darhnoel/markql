# AI Inspection Playbook

Use this workflow to combine `html_inspector` and MarkQL with minimal tokens.

For strict agent rules and anti-patterns, see [ai_markql_musts.md](./ai_markql_musts.md).

The AI should do only four things:

1. choose one row family
2. draft one small query
3. validate locally
4. escalate only if blocked

## Core Rule

Use the cheapest artifact that can answer the next question.

Escalation order:

1. `--families-compact`
2. `--families`
3. `--skeleton`
4. targeted subtree or HTML slice
5. full HTML

Do not jump to raw HTML unless all cheaper layers failed.

## Default Workflow

### Step 1. Start with compact families

Run:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --families-compact <path-or-url>
```

Give the AI only:
- user goal
- compact family output
- one tiny MarkQL syntax pack for the current step

Do not send raw HTML or full docs first.

### Step 2. Choose one family

Selection order:

1. prefer `D` over `H` and `U`
2. prefer higher repeat count
3. prefer table families for table-like goals
4. prefer informative slots such as `a[href]`, `img[src]`, `span`, `td>a[href]`
5. prefer `P` over `F` only if the row shape looks stable

Do not draft a full extraction query before one family is chosen.

### Step 3. Generate one small query

Use this progression:

1. row check
2. row gate
3. row-anchor `WITH`
4. one-field extraction
5. full `PROJECT`

Mode rule:
- `F` means start with `FLATTEN`
- `P` means a first-pass `PROJECT` may be acceptable, but `WITH` is still preferred once the query stops being trivial

Hard rule:
- `PROJECT(...)` and `FLATTEN(...)` are allowed inside CTE bodies, but use them only as pure shaped relations
- do not mix `PROJECT(...)` or `FLATTEN(...)` with row-id anchor columns in the same CTE
- use CTEs for one of two purposes:
  - row-anchor/scalar helper relations
  - pure shaped relations produced by `PROJECT(...)` or `FLATTEN(...)`
- if you still need stable row ids, joins, or node identity, keep `PROJECT(...)` or `FLATTEN(...)` in the final top-level `SELECT`

### Step 4. Validate locally

Use lint first, then execution if needed.

Feed back only the query plus a short result summary or compact diagnostics.

### Step 5. Escalate one level only when needed

Escalate only to answer a specific unresolved question, such as:
- which of two `div>div` families is the real card row
- whether a table family has optional cells
- whether the useful supplier is direct `a[href]` or nested

## How To Read Compact Families

Example:

```text
D6|table>tr|22|D|P|slot:td>a[href]|sig:td
```

Meaning: repeated data rows, anchor hypothesis `table > tr`, 22 rows, `PROJECT` is plausible, strongest supplier path likely starts at `td > a[href]`.

Good next move: a small row-anchor query or `WITH r_rows AS (...)` query, then `PROJECT` if the shape stays stable.

## Prompt Contract

Keep the prompt narrow.

```text
Goal: extract the main schedule table.
Artifact: <compact family output>
Task:
- choose one family id
- explain why in one sentence
- output one next MarkQL query only
- prefer FLATTEN if mode is F
- prefer a row-anchor CTE before a large extraction
- prefer PROJECT only if mode is P and the query is still simple
```

Do not send full HTML, full docs, skeleton, verbose families, and past failures in the first prompt.

## Tiny Retrieval Packs

Use only what the current step needs.

Row selection pack:
- `FROM doc`
- `WHERE`
- `EXISTS(child ...)`
- one small row-selection example

Exploration pack:
- `FLATTEN`
- `TEXT`
- `ATTR`
- one short exploratory example

Stabilization pack:
- `WITH r_rows AS (...)`
- one small anchor CTE example
- one small pure-shaped CTE example

Stable extraction pack:
- `PROJECT(...) AS (...)`
- `COALESCE`
- positional `TEXT(..., n)` or `ATTR(..., n)` if needed
- one small extraction example

Repair pack:
- one short example matching the lint failure

Query rules:
- one family at a time
- one query at a time
- row gating before field extraction
- `FLATTEN` for discovery
- use `WITH` to stabilize non-trivial queries
- `PROJECT` only when row shape and suppliers are clear
- if using `PROJECT(...)` or `FLATTEN(...)` inside a CTE, keep that CTE pure and do not combine it with row-id helper columns
- if uncertain, ask for the next artifact level instead of guessing a bigger query

## What Not To Do

Do not:
- send full HTML first
- send full docs first
- ask for a production query before row selection is stable
- make the AI infer repeated structure from raw DOM when family mode already exists
- feed raw verbose execution output back when a short summary will do
