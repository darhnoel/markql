# Compact Family Mode

`html_inspector --families-compact` emits a deterministic, compressed summary of repeated DOM families.

For the full low-token AI workflow that combines compact families, escalation rules, and MarkQL drafting, see [ai_inspection_playbook.md](./ai_inspection_playbook.md).

It is:
- not raw HTML
- not a full DOM dump
- not a semantic extractor
- not a final MarkQL query generator

It is a low-token guidance artifact for the first MarkQL drafting step. Its job is to help humans and AI quickly identify:
- likely row anchors
- likely repeated row families
- whether to start with `FLATTEN` or `PROJECT`
- the strongest structural supplier path inside each family

## CLI

Local file:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --families-compact docs/fixtures/basic.html
```

URL:

```bash
cargo run --manifest-path tools/html_inspector/Cargo.toml -- --families-compact https://example.com
```

## Output Shape

Example:

```text
FAM
KIND: D=data, H=header, U=unknown
MODE: F=FLATTEN, P=PROJECT

D4|ul>li|12|D|F|slot:div>a[href]|sig:div>p
D2|ul>li|9|D|F|slot:a[href]|sig:a[href]
D5|ul>li|3|D|F|slot:a[href]>img[src]|sig:a[href]>img[src]
```

Each family line is:

```text
<family_id>|<parent_tag>><repeated_tag>|<count>|<kind>|<mode>|slot:<slot_hint>|sig:<signature>
```

Field meanings:

| Field | Meaning |
| --- | --- |
| `D4` | Family id. Stable only within one run of the tool. |
| `ul>li` | Row anchor path: repeated `li` siblings under `ul`. |
| `12` | Repeat count for that exact sibling-local family. |
| `D` / `H` / `U` | Family kind: likely data, likely header/meta, or unknown. |
| `F` / `P` | Recommended first extraction mode: `FLATTEN` or `PROJECT`. |
| `slot:...` | Strongest structural supplier hint inside the repeated row. |
| `sig:...` | Shallow structural signature for fast family differentiation. |

Code legend:

- `D` = likely data family
- `H` = likely header or meta family
- `U` = unknown or ambiguous family
- `F` = start with `FLATTEN`
- `P` = start with `PROJECT`

## How A Human Should Read It

Use the line as a compressed hypothesis, not a conclusion.

- Prefer higher counts first. `ul>li|12` is usually more important than `ul>li|3`.
- `slot:a[href]` means the row likely exposes a useful link directly.
- `slot:a[href]>img[src]` usually means a banner, icon, or image-link row.
- `slot:div>a[href]` means the useful node is nested inside a container.
- `sig:div>p` means the row starts with a `div` child, then a shallow nested `p`.
- `F` means “explore first”. The family looks useful, but the tool is not claiming stable field suppliers yet.
- `P` means “structure is regular enough that a first-pass `PROJECT` may be reasonable”.

Example:

```text
D4|ul>li|12|D|F|slot:div>a[href]|sig:div>p
```

Read it as:

- there is a likely data family
- it is a repeated `li` row under `ul`
- there are 12 such rows
- start with `FLATTEN`
- the strongest supplier path is a nested `div > a[href]`
- the row shape is shallowly consistent with `div > p`

## How An AI Agent Should Use It

Treat the compact line as a decoding contract.

Use these rules:

1. Prefer `D` families over `H` and `U`.
2. Prefer higher counts first.
3. Use `parent>repeated` as the initial row-family hypothesis.
4. Use `slot:` as the strongest supplier-path hint.
5. Use `sig:` only as shallow shape confirmation, not as a full DOM substitute.
6. If mode is `F`, draft a `FLATTEN`-based exploratory query first.
7. If mode is `P`, a `PROJECT`-based first pass may be acceptable.
8. If multiple families share the same anchor path, use count and slot specificity to choose between them.

Important constraint:
- `F` and `P` describe the extraction step, not just the row-anchor step.
- `FLATTEN(...)` and `PROJECT(...)` may appear inside `WITH ... AS (...)`, but only as pure shaped relations.
- Do not mix `FLATTEN(...)` or `PROJECT(...)` with row-id helper columns inside the same CTE.
- Use `WITH` either to anchor rows and carry ids, or to build a pure shaped relation, but do not blur those two roles.

Compact mode is intentionally lossy. An AI should escalate to `--families` or `--skeleton` when the compact artifact is not enough to separate candidate families.

## MarkQL Drafting Guidance

Given:

```text
D2|ul>li|9|D|F|slot:a[href]|sig:a[href]
```

The intended high-level interpretation is:

- likely row anchor: repeated `li`
- repeated context: under `ul`
- likely useful descendant: `a[href]`
- first step: explore the row with `FLATTEN`, not `PROJECT`

The intended workflow stays staged:

1. identify rows
2. narrow rows
3. extract one field
4. scale to a stable schema later

Compact mode helps with step 1. It does not replace the later validation and refinement steps.

## Limitations

- Folding is exact-match only.
- Grouping is sibling-local only.
- Compact mode is intentionally lossy.
- Small structural differences can split similar rows into separate families.
- A detected family is only a candidate extraction target.
- Family ids are run-local labels, not persistent identifiers.
- `slot:` is one strongest hint, not a full field map.
- `sig:` is shallow by design and must not be treated as a complete subtree description.
- For debugging or difficult pages, use `--families` or `--skeleton` to inspect more detail.

## Choosing Between Modes

- Use `--families-compact` for AI prompting and quick triage.
- Use `--families` when you need the fuller family summary.
- Use `--skeleton` when you need broader DOM context.
