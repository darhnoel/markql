# MarkQL Exercises Workbook

This workbook is a verified, hands-on path from zero to real extraction workflows.

## Story First: What You Are Solving

You are the data owner for a scraping pipeline. Product asks for reliable CSV exports from messy HTML pages.

The real problem is not "write one query." The real problem is:

- choose the right row nodes
- avoid brittle selectors
- extract stable columns
- detect drift before shipping bad data

Each module is one phase of that story.

## Big-Picture Pipeline

```text
Source -> Select rows -> Stabilize rows -> Validate -> Export
                         (later)
        -> Shape transform (FLATTEN) -> Column extraction (PROJECT)
```

## Learning Path

- Beginner
  - `00_setup`: learn the controls (run, load, inspect, export)
  - `01_basics_select_where`: isolate the right rows
- Intermediate
  - `02_row_stability_debug`: debug noisy/empty row sets
  - `03_flatten_shape_transform`: use FLATTEN only for repeated shape
- Advanced
  - `04_project_primary_extraction`: build stable named columns with PROJECT
  - `99_capstone`: ship end-to-end extraction with verification

## Module Questions (Why each exists)

- `00_setup`: Can you run MarkQL confidently without fighting tooling?
- `01_basics_select_where`: Are you selecting the intended rows, not "close enough" rows?
- `02_row_stability_debug`: Can you recover quickly when queries return zero or noisy rows?
- `03_flatten_shape_transform`: Do you know when positional flattening is appropriate?
- `04_project_primary_extraction`: Can you separate row gating from field extraction?
- `99_capstone`: Can you produce deterministic output and prove it is correct?

## Capstone Outcomes

By the end, you can:

- build stable row filters that survive class churn
- extract named columns from row nodes with PROJECT
- use FLATTEN only when repeated nested shape is the real problem
- export deterministic CSV output and verify it with a script

## How to Run Verification

From repository root:

```bash
docs/exercises/scripts/verify_exercises.sh
```

Pass criteria:

- every `*_solution.sql` (and capstone `solution.sql`) runs successfully
- generated CSV output matches the corresponding expected CSV file

## Safe Expected-Output Updates

Only update expected outputs after intentionally changing fixtures or solution queries.

```bash
docs/exercises/scripts/verify_exercises.sh --update
```

Then review `git diff` for every changed `*_expected.csv` / `expected.csv`.

## CI Guidance

Run this in CI on every PR:

```bash
docs/exercises/scripts/verify_exercises.sh
```

A non-zero exit means at least one exercise drifted.

## Verifier Self-Test

The repository includes a small pass/fail harness check for `docs/exercises/scripts/verify_exercises.sh`:

```bash
docs/exercises/scripts/test_verify_exercises.sh
```

This confirms:

- pass fixtures succeed
- intentional mismatch fixtures fail
