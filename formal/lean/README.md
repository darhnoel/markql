# MarkQL Formal Subset

This directory contains the opt-in Lean 4 milestone for phase 1 of the formal-verification plan.

## Scope

The current formal subset is exactly:

```text
SELECT <identifier|*> FROM doc [AS <alias>]
```

Included:

- `SELECT div FROM doc`
- `SELECT * FROM doc`
- `SELECT div FROM doc AS n`

Excluded on purpose:

- `WHERE`
- `JOIN`
- `WITH`
- `ORDER BY`
- `LIMIT`
- `TO`
- `RAW(...)`
- `PARSE(...)`
- field-qualified projections such as `a.href`

The current model also rejects reserved alias `self` inside this subset.

## What is proven

- A tiny reference lexer and parser exist for the phase-1 subset only.
- Theorem-backed examples prove acceptance for canonical valid subset queries.
- Theorem-backed examples prove rejection for malformed subset-adjacent queries.

## What is not proven

- The full MarkQL grammar.
- The current production engine parser.
- Runtime semantics or DOM behavior.
- Exact diagnostic wording.
- Any query surface outside the phase-1 subset.

Queries that are valid full MarkQL but intentionally outside this tiny subset are not treated here as "invalid MarkQL". They are simply out of scope for phase 1.

## How to run Lean checks

From the repository root:

```bash
cd formal/lean
lake build
```

To compile the theorem module directly:

```bash
cd formal/lean
lake env lean MarkQLCore/Theorems.lean
```

## How to run conformance checks

From the repository root:

```bash
./scripts/run_formal_conformance.sh
```

The conformance runner compares only:

- `accept` => engine lint exit code `0`
- `reject` => engine lint exit code `1`

It does not compare full diagnostic text in phase 1.
