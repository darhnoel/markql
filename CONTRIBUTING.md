# Contributing

## Scope

MarkQL is a SQL-style query engine for HTML. Keep row selection and field extraction explicit and separate:

- outer `WHERE` decides row survival
- field expressions decide supplier/value selection

Preserve backward compatibility unless repo docs explicitly mark a path as deprecated or experimental.

## Development Rules

- Keep parser, validator, executor, and CLI boundaries intact. Parser code must not depend on runtime modules.
- When changing language behavior, update the full chain: grammar/parser, AST, validation, execution, tests, and docs.
- Keep diagnostics deterministic, actionable, and stage-aware.
- Do not use live network access in integration tests.
- Separate refactor-only changes from semantic changes when practical so review can distinguish behavior from structure.

## File Size Guardrail

- Tracked source files should stay below `1000` lines.
- If a source file approaches the limit, split it along existing module boundaries before adding more behavior.
- Prefer small internal headers plus additional translation units over growing a catch-all implementation file.
- Run `python3 scripts/maintenance/check_loc.py` before sending maintainability-heavy changes.

## Formatting

- Repository source-code formatting is separate from any future MarkQL query formatter work. Do not treat `.clang-format` as a query-language formatting contract.
- The repository formatter baseline currently covers C++ only via `.clang-format`.
- Match the surrounding file style and keep formatting scoped to the files or lines you touched.
- Prefer explicit paths or a branch diff when formatting:
  - `./scripts/format/format_cpp.sh path/to/file.cpp`
  - `./scripts/format/format_cpp.sh --diff-base origin/main`
  - `./scripts/format/check_cpp_format.sh path/to/file.cpp`
  - `./scripts/format/check_cpp_format.sh --diff-base origin/main`
- The tracked repository-owned C++ tree is expected to stay clean under `./scripts/format/check_cpp_format.sh --all`.
- `--all` exists for one-time normalization work, but whole-tree reformatting is intentionally not the default because it creates noisy review churn.

## Query and CLI Changes

- Follow the first-query loop: inspect rows, gate rows, extract one value, then scale to a fuller schema.
- Keep shaped CTEs pure; do not mix row-id helper CTEs with `PROJECT(...)` or `FLATTEN(...)` CTEs.
- If query syntax, semantics, diagnostics, or CLI contracts change, update the relevant docs and examples in the same change.

## Validation

Run the smallest relevant verification set for the change:

- `./scripts/build/build.sh`
- `./scripts/test/ctest.sh --output-on-failure`
- `./build/markql --lint "<query>"`
- `python3 scripts/maintenance/check_loc.py`
- `./docs/verify_examples.sh` when docs examples change
- `./scripts/python/test.sh` when Python bindings change
- `./tests/integration/run_cucumber.sh` when CLI behavior changes

## CI Guardrails

- GitHub Actions enforces the LOC guardrail on tracked source files.
- Pull requests also run a changed-file C++ formatting check using `.clang-format`.
- Build and C++ test coverage remain part of CI so maintainability tooling does not diverge from normal validation.

## Collaboration

- Do not revert unrelated work in a dirty tree.
- Keep commits and patches scoped.
- Add brief comments only where the code would otherwise be hard to parse.
