# Test Placement Migration Note

Use this placement guide for new tests:

- New C++ unit/component tests:
  - Keep using current C++ harness and register via CMake/CTest.
  - Prefer placing new files under `tests/unit/` once CMake source lists are migrated.
  - Until then, existing `tests/test_*.cpp` placement remains valid.
- New CLI black-box behavior tests:
  - Add `.feature` files to `tests/integration/features/`.
  - Add Rust step/world logic in `tests/integration/rust/`.
- New docs command regressions:
  - Keep `docs/verify_examples.sh` as the canonical runner.
  - Add any test-topology wrappers in `tests/docs_regression/`.
- Benchmarks:
  - Keep under `bench/` only (no correctness gating assumptions).
- Exploratory/manual scripts:
  - Keep under `local_tests/`.
