# Changelog

All notable changes to XSQL will be documented in this file.

This project follows a Keep a Changelog style and uses Semantic Versioning.

## [Unreleased]

## [1.4.0] - 2026-02-07

### Added
- Added `RAW_INNER_HTML(tag[, depth])` to return raw inner HTML without minification.
- Added `util::minify_html(std::string_view)` as a shared HTML whitespace minifier helper.
- Added benchmark target `xsql_bench_inner_html` to compare minified vs raw inner HTML output.
- Added tests for minifier behavior and proportional cursor mapping.

### Changed
- `INNER_HTML(tag[, depth])` now returns minified HTML by default.
- REPL cursor movement with Up/Down now maps cursor position proportionally across lines and history entries instead of using fixed character index.
- Bumped project version to `1.4.0` in build/package metadata.

### Notes
- If you need pre-1.4.0 raw spacing behavior, switch from `INNER_HTML(...)` to `RAW_INNER_HTML(...)`.
