# MarkQL Test Topology

This directory groups test assets by intent while keeping existing test flows stable.

## Categories

- `unit/component`:
  Existing C++ test binaries and CTest cases (current sources stay at `tests/test_*.cpp` for now).
- `integration (CLI black-box)`:
  End-to-end tests that execute the real `markql` binary using local fixtures only.
  Gherkin features live in `tests/integration/features/`.
  Rust Cucumber runner lives in `tests/integration/rust/`.
- `docs-regression`:
  Documentation command verification (delegates to `docs/verify_examples.sh`).
  Wrapper lives in `tests/docs_regression/`.
- `performance benchmarks`:
  `bench/` (not correctness regression tests).
- `exploratory/manual`:
  `local_tests/` scripts and ad-hoc experiments.

## Run Commands

- Existing C++ tests (current flow):
  - `./scripts/test/ctest.sh`
  - or `ctest --test-dir build --output-on-failure`
- Docs verification (existing flow, unchanged):
  - `./docs/verify_examples.sh`
  - or categorized wrapper: `./tests/docs_regression/run_docs_verify.sh`
- Rust Cucumber CLI integration tests:
  - `./tests/integration/run_cucumber.sh`
  - optional binary override: `MARKQL_BIN=/absolute/or/relative/path/to/markql ./tests/integration/run_cucumber.sh`

## Notes

- `tests/fixtures/` and `tests/golden/` remain shared fixture/snapshot roots.
- No live network is allowed in integration tests; use local fixtures only.
- See `tests/MIGRATION.md` for where future tests should be added.
