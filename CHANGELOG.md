# Changelog

All notable changes to MarkQL will be documented in this file.

This project follows a Keep a Changelog style and uses Semantic Versioning.
Historical entries were backfilled from git commit history on 2026-02-07 and focus on major changes on `main` (not every docs/chore commit).

## [Unreleased]

### Added
- Added versioned artifact snapshots:
  - `.mqd` for serialized parsed-document snapshots
  - `.mqp` for serialized prepared queries after parse + validate.
- Added repo-local `vcpkg` manifest support for FlatBuffers so `DOCN` schema generation and runtime linkage stay reproducible without system package installs.
- Added CLI artifact commands:
  - `--write-mqd` to persist the parsed `doc` input once
  - `--write-mqp` to persist a prepared query once
  - `--artifact-info` to inspect artifact metadata and compatibility.
- Added transparent CLI execution support for:
  - running normal SQL text against `.mqd`
  - running `.mqp` against HTML/stdin/URL input
  - running `.mqp` against `.mqd`.
- Added artifact tests for direct-vs-artifact result equivalence, deterministic bytes, major-version rejection, and corruption safety.
- Added `markql_bench_artifacts` benchmark covering HTML parse, `.mqd` write/read, parsed-document execution, `.mqd`-loaded execution, and artifact size.
- Added `PARSE(...)` as a source constructor for parsing HTML strings into queryable node streams.
- Added `PARSE(...)` support for both scalar HTML expressions and subquery-produced HTML strings.
- Added SQL-style row alias field binding with `alias.field` across projections/predicates.
- Added implicit `doc` row alias binding for plain `FROM doc` / `FROM document`.
- Added alias parser/evaluator tests for implicit `doc` binding, explicit alias binding, and alias error paths.
- Added `WITH` (CTE) support with ordered CTE evaluation and statement-local scope.
- Added SQL-style join chains in `FROM`: `JOIN`, `LEFT JOIN`, `CROSS JOIN`, and `CROSS JOIN LATERAL`.
- Added derived-table sources: `FROM (SELECT ...) AS alias`.
- Added parser and evaluator coverage for CTE/join/lateral acceptance, rejection, and deterministic baseline outputs.
- Added Python smoke coverage for `WITH` + `LEFT JOIN` + `CROSS JOIN LATERAL`.
- Added a structured diagnostics layer for syntax + semantic query failures with:
  - severity, stable diagnostic code, message, help, docs reference
  - byte/line-column spans
  - caret-style code-frame snippets
  - optional related spans.
- Added CLI lint mode: `--lint` for parse+validate-only diagnostics (no execution).
- Added lint machine output: `--lint --format json` with stable diagnostic JSON schema.
- Added unified diagnostic rendering in CLI/script error paths (replacing raw exception-only output).
- Added core/Python version provenance APIs (`version + git commit + dirty flag`).
- Added canonical current-row node projection syntax: `SELECT self`.
- Added lint warning `MQL-LINT-0001` for ambiguous `SELECT <from_alias>` value projection with migration help to `SELECT self`.
- Added parser/runtime/tests/docs coverage for `SELECT self` in plain and `CROSS JOIN LATERAL` node-stream flows.
- Added lint diagnostics color controls in CLI text mode:
  - `--color=auto|always|never|disabled`
  - `NO_COLOR` override support
  - TTY-aware `auto` behavior.
- Added diagnostics renderer styling option for severity-aware ANSI text formatting (human output only).
- Added tests for diagnostics color on/off behavior, JSON ANSI guards, and CLI color policy precedence.
- Added `MARKQL_BENCH_STATS` (opt-in env toggle) to report PROJECT selector hot-path counters for benchmarking.

### Deprecated
- Deprecated `FRAGMENTS(...)` in favor of `PARSE(...)`.
- `FRAGMENTS(...)` remains supported for backward compatibility and now emits a deprecation warning.

### Changed
- Official rename completed across the tracked repository: internal namespaces, headers, CMake targets, CLI/test target names, install paths, docs, and CI now use `markql` / `MarkQL` consistently, with `pyxsql` preserved as the only intentional legacy identifier.
- Parse diagnostics now use a reusable expected-token-family repair layer before generic fallback, so operator/keyword/axis/value typos in local contexts such as `EXISTS(...)`, `SHOW`, `DESCRIBE`, `TO`, and `TABLE(...)` options produce local repair guidance and examples instead of unrelated top-level query help.
- Lint now reports coverage-aware summaries (`parse_only` / `full` / `reduced`), emits cautious clean-result wording (`no proven diagnostics`), returns a top-level JSON lint result envelope for CLI/editor use, and adds non-breaking suspicion/binding warnings for likely mistakes in reduced-coverage relation-style queries.
- Upgraded lint diagnostics to provide clearer mistake-specific messages, categories, `why` explanations, short valid examples, and additive JSON/Python fields (`category`, `why`, `example`, `expected`, `encountered`) while keeping the existing C++ lint engine authoritative.
- Marked `.mqd` / `.mqp` artifact workflows as experimental in CLI help and documentation so WIP builds do not imply a fully settled interface.
- `build.sh` now detects a default `vcpkg` triplet for Linux, macOS, and Windows environments instead of hardcoding `x64-linux`, and it falls back to portable CPU-count detection when `nproc` is unavailable.
- Artifact persistence now uses explicit file magic, format versioning, producer-major compatibility checks, and additive sectioned payloads instead of private runtime layouts.
- `.mqd` document artifacts keep the existing MarkQL outer envelope and store the `DOCN` section as a FlatBuffers payload verified after header/checksum validation.
- `.mqp` prepared-query artifacts now keep the same outer envelope while storing the `QAST` section as a FlatBuffers payload verified after header/checksum validation.
- `.mqp` readers keep a narrow legacy fallback for older manual-`QAST` artifacts when the FlatBuffers required-feature bit is absent.
- Artifact readers now treat `.mqd` and `.mqp` as untrusted data:
  - strict UTF-8 validation for all persisted text fields
  - bounded file/section/string/node/attribute/count parsing
  - payload checksum verification
  - rejection of unknown required feature flags
  - terminal-safe escaping for artifact-derived CLI metadata output.
- Added truth-first artifact benchmark reporting for cold-path comparisons between:
  - raw HTML + query text
  - raw HTML + `.mqp`
  - `.mqd` + query text
  - `.mqd` + `.mqp`
  along with separated phase timings for HTML read/parse, `.mqd` load, query parse/prepare, `.mqp` load, and execution on parsed-vs-loaded documents.
- `execute_query_from_file(...)` now accepts `.mqd` inputs transparently while preserving existing HTML-file behavior.
- `FROM doc AS <alias>` is now accepted directly (for example `FROM doc AS n`).
- `SELECT self` is now the documented canonical form for returning the current node in node-stream queries.
- Alias misuse now emits clearer errors:
  - `Identifier 'doc' is not bound; did you mean '<alias>'?`
  - `Unknown identifier '<x>' (expected a FROM alias or legacy tag binding)`
  - `Duplicate source alias '<x>' in FROM`
- Updated tutorial/grammar/case-study examples to prefer `SELECT self` in node-returning `LATERAL` subqueries.
- `--lint --format json` remains deterministic and ANSI-free regardless of color mode.
- Optimized PROJECT/FLATTEN_EXTRACT evaluation by introducing per-row selector scope/tag caching, reducing repeated subtree scans while preserving query results and output formatting.
- Bumped project/core, Python package metadata, and `vcpkg` manifest version references to `1.20.0`.

## [1.8.0] - 2026-02-13

### Added
- Added standalone DOM Explorer mode (`markql explore`) and REPL `.explore` command integration.
- Added two-pane Explorer UI with collapsed DOM tree (left) and structured node detail panels (right).
- Added Explorer search UX:
  - boxed search bar
  - fuzzy search over node `inner_html`
  - UTF-8 search input support (including non-Latin scripts)
  - match highlighting in `Inner HTML Head`
  - `n`/`N` navigation across visible matches
  - `j`/`k` scrolling inside `Inner HTML Head`
- Added in-session Explorer state restore per input target (selection, expanded nodes, zoom, search, scroll).
- Added unit tests for Explorer tree flattening, right-pane rendering, and search behavior/performance paths.

### Changed
- Improved Explorer rendering:
  - boxed right-pane sections (`Node`, `Inner HTML Head`, `Attributes`)
  - larger default vertical allocation to `Inner HTML Head`
  - better ANSI-safe rendering for highlighted matches.
- Improved fuzzy ranking quality by prioritizing word-level contiguous matches over weaker subsequence hits.
- Improved Explorer search performance on large documents:
  - prefix-result caching and candidate-restricted rescans
  - reduced search-path allocations
  - bounded match-window rendering for large inner-html blobs
  - debounced live search with immediate `Enter` execution.
- Bumped project version to `1.8.0`.

## [1.7.0] - 2026-02-10

### Added
- Added deterministic output column normalization for result schemas, including:
  - `normalize_colname(...)` helper with SQL-friendly identifier rules
  - collision suffixing (`name`, `name__2`, `name__3`, ...)
- Added REPL setting `.set colnames raw|normalize` (default: `normalize`).
- Added `DESCRIBE LAST` in REPL to show the last raw-to-output column name mapping.
- Added tests for column-name normalization rules, collisions, duckbox headers, CSV headers, and JSON key behavior.

### Changed
- Result headers now default to normalized identifier-safe names in:
  - REPL duckbox rendering
  - CSV export headers
  - JSON/NDJSON keys
  - Parquet field names
- Raw mode now keeps original column names (`.set colnames raw`) while preserving deterministic collision suffixing.
- Bumped project version to `1.7.0`.

## [1.6.0] - 2026-02-10

### Added
- Added `CASE WHEN ... THEN ... [ELSE ...] END` expressions for:
  - `SELECT` expression projections
  - `PROJECT(...) AS (alias: expr, ...)` mappings
- Added stable selector picking for scoped extraction:
  - `TEXT(..., n)` / `ATTR(..., n)` with 1-based indexes
  - `FIRST_TEXT(...)`, `LAST_TEXT(...)`, `FIRST_ATTR(...)`, `LAST_ATTR(...)`
- Added `TO JSON(...)` export sink (single JSON array of row objects).
- Added `TO NDJSON(...)` export sink (one JSON object per line).
- Added stdout export fallback for JSON sinks via `TO JSON()` / `TO NDJSON()`.
- Added parser/runtime/export tests for:
  - CASE parsing and evaluation
  - selector index semantics (first/nth/last and out-of-range nulls)
  - JSON/NDJSON file output and stdout fallback

### Changed
- Updated `SHOW FUNCTIONS` / `DESCRIBE language` metadata for CASE, selector helpers, and JSON sinks.
- Updated CLI help/autocomplete and docs to include JSON/NDJSON export and CASE/selectors.
- Bumped project version to `1.6.0`.

## [1.5.0] - 2026-02-10

### Added
- Added SQL-style `LIKE` operator with `%` and `_` wildcard semantics.
- Added string functions across query expressions: `CONCAT`, `SUBSTRING`/`SUBSTR`, `LENGTH`/`CHAR_LENGTH`,
  `POSITION`, `LOCATE`, `REPLACE`, `LOWER`, `UPPER`, `LTRIM`, `RTRIM`, `TRIM`, and `DIRECT_TEXT`.
- Added support for function expressions in `SELECT` projections and `WHERE` predicates
  (for example `LOWER(TEXT(div)) LIKE '%foo%'` and `POSITION('x' IN TEXT(div)) > 0`).
- Added parser/evaluator tests for LIKE, string functions, DIRECT_TEXT behavior, and PROJECT regression coverage.
- Added dedicated test suite block `test_string_sql.cpp` with parser + evaluator + PROJECT semantics checks.
- Restored `EXISTS(axis [WHERE expr])` predicate support in parser, AST, executor, and validation.
- Added predicate tests for `EXISTS(child)`, `EXISTS(child WHERE tag = ...)`, and same-node matching behavior.
- Added `FLATTEN_EXTRACT(tag) AS (alias: expr, ...)` projection support with expression mapping:
  `TEXT(tag WHERE ...)`, `ATTR(tag, attr WHERE ...)`, and `COALESCE(...)`.
- Added dedicated `FLATTEN_EXTRACT` tests for extraction, `HAS_DIRECT_TEXT` predicate usage, and syntax validation.
- Added reserved keyword `PROJECT` as the canonical syntax for structured extraction:
  `PROJECT(tag) AS (alias: expr, ...)`.

### Changed
- `PROJECT(...)`/`FLATTEN_EXTRACT(...)` expressions now support nested SQL string functions, literals,
  and alias references in `alias: expression` mappings.
- `DIRECT_TEXT(tag)` now uses strict immediate-text extraction (descendant text is excluded).
- Updated language metadata (`SHOW FUNCTIONS`, `SHOW OPERATORS`, `DESCRIBE language`) to include LIKE and new string functions.
- Bumped project version to `1.5.0`.
- Updated CLI and tutorial docs to document `EXISTS(...)` syntax, supported axes (`self|parent|child|ancestor|descendant`), and inner `WHERE` semantics.
- Updated docs and language metadata (`SHOW FUNCTIONS` / `DESCRIBE language`) to include `FLATTEN_EXTRACT` usage.
- Updated docs and language metadata to prefer `PROJECT(...)`; `FLATTEN_EXTRACT(...)` remains a compatibility alias.
- Rebranded user-facing CLI/documentation name to MarkQL while keeping internal `markql` namespace and APIs unchanged.
- REPL prompt is now `markql> `, and the default CLI binary output is now `markql` (with `markql` compatibility binary still generated).
- REPL history recall now places the cursor at end-of-line by default when navigating with Up/Down.

### Deprecated
- `HAS_DIRECT_TEXT` remains supported, but `DIRECT_TEXT(tag) LIKE '%...%'` is the preferred SQL-style form.

## [1.4.0] - 2026-02-07
Includes major changes first landed between 2026-01-12 and 2026-02-07.

### Added
- Added `RAW_INNER_HTML(tag[, depth])` to return raw inner HTML without minification.
- Added `util::minify_html(std::string_view)` as a shared HTML whitespace minifier helper.
- Added `FLATTEN_TEXT(...)` projection support with optional depth, then added `FLATTEN(...)` as an alias (first landed 2026-01-19).
- Added `max_depth` and `doc_order` metadata fields in result rows (first landed 2026-01-19).
- Added Khmer number conversion support as both CLI module and plugin (first landed 2026-01-18).
- Added benchmark target `markql_bench_inner_html` to compare minified vs raw inner HTML output.
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
