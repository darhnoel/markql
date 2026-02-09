# Changelog

All notable changes to MarkQL will be documented in this file.

This project follows a Keep a Changelog style and uses Semantic Versioning.
Historical entries were backfilled from git commit history on 2026-02-07 and focus on major changes on `main` (not every docs/chore commit).

## [Unreleased]

### Added
- Restored `EXISTS(axis [WHERE expr])` predicate support in parser, AST, executor, and validation.
- Added predicate tests for `EXISTS(child)`, `EXISTS(child WHERE tag = ...)`, and same-node matching behavior.
- Added `FLATTEN_EXTRACT(tag) AS (alias: expr, ...)` projection support with expression mapping:
  `TEXT(tag WHERE ...)`, `ATTR(tag, attr WHERE ...)`, and `COALESCE(...)`.
- Added dedicated `FLATTEN_EXTRACT` tests for extraction, `HAS_DIRECT_TEXT` predicate usage, and syntax validation.
- Added reserved keyword `PROJECT` as the canonical syntax for structured extraction:
  `PROJECT(tag) AS (alias: expr, ...)`.

### Changed
- Updated CLI and tutorial docs to document `EXISTS(...)` syntax, supported axes (`self|parent|child|ancestor|descendant`), and inner `WHERE` semantics.
- Updated docs and language metadata (`SHOW FUNCTIONS` / `DESCRIBE language`) to include `FLATTEN_EXTRACT` usage.
- Updated docs and language metadata to prefer `PROJECT(...)`; `FLATTEN_EXTRACT(...)` remains a compatibility alias.
- Rebranded user-facing CLI/documentation name to MarkQL while keeping internal `xsql` namespace and APIs unchanged.
- REPL prompt is now `markql> `, and the default CLI binary output is now `markql` (with `xsql` compatibility binary still generated).
- REPL history recall now places the cursor at end-of-line by default when navigating with Up/Down.

## [1.4.0] - 2026-02-07
Includes major changes first landed between 2026-01-12 and 2026-02-07.

### Added
- Added `RAW_INNER_HTML(tag[, depth])` to return raw inner HTML without minification.
- Added `util::minify_html(std::string_view)` as a shared HTML whitespace minifier helper.
- Added `FLATTEN_TEXT(...)` projection support with optional depth, then added `FLATTEN(...)` as an alias (first landed 2026-01-19).
- Added `max_depth` and `doc_order` metadata fields in result rows (first landed 2026-01-19).
- Added Khmer number conversion support as both CLI module and plugin (first landed 2026-01-18).
- Added benchmark target `xsql_bench_inner_html` to compare minified vs raw inner HTML output.
- Added tests for minifier behavior and proportional cursor mapping.

### Changed
- `INNER_HTML(tag[, depth])` now returns minified HTML by default.
- REPL cursor movement with Up/Down now maps cursor position proportionally across lines and history entries instead of using fixed character index.
- Improved line editor key handling (Delete key behavior; first landed 2026-01-21).
- Consolidated docs: compact top-level README and moved tutorial-style documentation into `docs/`.
- Added auto-generated syntax diagrams in `docs/diagrams` (first landed 2026-01-17).
- Bumped project version to `1.4.0` in build/package metadata.

### Notes
- If you need pre-1.4.0 raw spacing behavior, switch from `INNER_HTML(...)` to `RAW_INNER_HTML(...)`.

## [1.3.1] - 2026-01-17
Includes major changes first landed on 2026-01-17.

### Added
- Added duckbox row-count footer output and aligned docs/examples.

## [1.3.0] - 2026-01-17
Includes major changes first landed between 2026-01-11 and 2026-01-17.

### Added
- Added `DESCRIBE language` meta query with test coverage.
- Extended REPL/source workflow with `.load --alias`, multi-source state, and TOML-based config/history settings.

### Changed
- Improved REPL multiline input handling and continuation behavior.
- Expanded `~` and `$HOME` in REPL history path settings.

## [1.2.1] - 2026-01-11
Includes patch changes first landed on 2026-01-11.

### Fixed
- Improved `.load` URL parsing and enabled curl decompression for URL inputs.

### Notes
- `1.2.1` was reflected in docs/README messaging; package metadata remained at `1.2.0` until the next version bump.

## [1.2.0] - 2026-01-11
Includes major changes first landed between 2026-01-06 and 2026-01-11.

### Added
- Added `TFIDF(...)` aggregate pipeline and CLI display mode control.
- Added `TO TABLE(...)` extraction/export options with header control.
- Added `RAW(...)` and `FRAGMENTS(...)` sources including subquery fragment parsing.
- Added predicate features: `HAS_DIRECT_TEXT`, `CONTAINS`, `CONTAINS ALL`, `CONTAINS ANY`, and parenthesized `WHERE` expressions.
- Added plugin system support (including Khmer segmenter wrapper).
- Added `sibling_pos` filtering and stricter URL HTML content validation.

### Changed
- Refactored parser/core/CLI modules and test layout for maintainability.
- Added execution guardrails with dedicated test coverage.

## [1.0.0] - 2026-01-04
Includes foundational changes first landed between 2025-12-30 and 2026-01-04.

### Added
- Initial stable release line with SQL-style HTML querying CLI.
- Added `EXCLUDE`, `node_id` filtering, and numeric literals in `WHERE`.
- Added REPL autocomplete and keyword-highlighting improvements.
- Added `.max_rows inf` alias support.
- Added `TO CSV(...)` and `TO PARQUET(...)` export sinks with Arrow integration.
- Added Python package (`pybind11`) and Python test workflow.
- Added CI workflows for wheel builds and publish pipeline.

### Fixed
- Fixed `TO TABLE` duckbox header/value mapping to avoid `NULL` row rendering artifacts.
