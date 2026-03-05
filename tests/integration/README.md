# CLI Integration Tests (Rust + Cucumber)

This suite is a black-box contract for the MarkQL CLI:

- Executes the real binary via `std::process::Command`.
- Uses only local fixtures (`docs/fixtures`, `tests/fixtures`).
- Asserts exit code, stdout/stderr content, and stable output contracts.

## Layout

- `features/`: Gherkin scenarios.
- `rust/`: Rust Cucumber runner and step definitions.
- `run_cucumber.sh`: convenience wrapper.

## Run

```bash
./tests/integration/run_cucumber.sh
```

Override binary path if needed:

```bash
MARKQL_BIN=./build/markql ./tests/integration/run_cucumber.sh
```
