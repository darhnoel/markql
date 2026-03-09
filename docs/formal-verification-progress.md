# Formal Verification Progress

## Status

- current branch: `lean/formal-check-foundation`
- current phase: `phase 1`
- overall status: `first proof-backed subset milestone implemented and locally validated`

## Completed

- added an isolated Lean 4 project under `formal/lean/`
- implemented a tiny AST for projection, fixed `doc` source, and optional alias
- implemented a tiny token-based lexer for the phase-1 subset
- implemented a tiny parser for `SELECT <identifier|*> FROM doc [AS <alias>]`
- encoded 5 accepted examples and 6 rejected examples in Lean
- added theorem-backed acceptance and rejection checks
- added an engine-independent JSON case corpus
- added an opt-in CLI adapter for the current engine lint interface
- added an opt-in conformance runner that checks verdict and exit code only
- validated `lake build` for the isolated Lean package
- validated direct theorem checking with `lake env lean MarkQLCore/Theorems.lean`
- validated adapter-backed conformance against the current CLI
- kept all work isolated from the default build and production parser/runtime

## Current subset

```text
SELECT <identifier|*> FROM doc [AS <alias>]
```

Notes:

- source is fixed to `doc`
- alias is optional
- alias `self` is rejected in the current formal subset
- no `WHERE`, `JOIN`, `WITH`, `ORDER BY`, `LIMIT`, `TO`, `RAW(...)`, or `PARSE(...)`

## Theorems implemented

- `parse_accept_select_div_from_doc`: canonical identifier projection parses successfully.
- `parse_accept_select_star_from_doc`: canonical star projection parses successfully.
- `parse_accept_select_div_from_doc_as_n`: canonical aliased identifier projection parses successfully.
- `parse_accept_select_title_from_doc`: another identifier projection parses successfully.
- `parse_accept_select_star_from_doc_as_node`: aliased star projection parses successfully.
- `parse_reject_select_from_doc`: missing projection is rejected.
- `parse_reject_select_div_doc`: missing `FROM` is rejected.
- `parse_reject_from_doc_select_div`: wrong clause order is rejected.
- `parse_reject_select_div_from`: missing source is rejected.
- `parse_reject_select_div_as_n`: alias without `FROM doc` is rejected.
- `parse_reject_select_div_from_doc_as_self`: reserved alias `self` is rejected.
- `accepted_example_count`: confirms the accepted example set size.
- `rejected_example_count`: confirms the rejected example set size.
- `accepted_examples_are_accepted`: all accepted examples evaluate to `true` under `isAccepted`.
- `rejected_examples_are_rejected`: all rejected examples evaluate to `true` under `isRejected`.

## Conformance artifacts

- `tests/formal_conformance/core_select_doc_alias_cases.json`
- `scripts/engine_adapters/cli_json_adapter.sh`
- `scripts/run_formal_conformance.sh`

## Decisions made

- used a tiny whitespace-splitting lexer to keep phase 1 explicit and small
- kept keyword handling canonical instead of modeling broader case-insensitive behavior
- omitted valid full-MarkQL forms that are outside the phase-1 subset from the case corpus
- kept conformance checks at verdict and exit-code level only
- treated adapter normalization as the only backend-specific layer

## Known gaps

- the formal model does not cover `document` synonym, even though full MarkQL does
- the formal model does not cover diagnostics beyond accept or reject
- the conformance runner does not compare syntax vs semantic diagnostic family yet
- no CI wiring has been added

## Next recommended step

- add one tiny CI job that runs `cd formal/lean && lake build` and `./scripts/run_formal_conformance.sh`
