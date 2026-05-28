# Functions

Status: draft skeleton

This file defines MarkQL functions and helper semantics.

## FUNC-001: Text Helpers

Text helper functions include `TEXT`, `DIRECT_TEXT`, `FIRST_TEXT`, and `LAST_TEXT`.

The function argument may be a row reference or a supplier selector, depending on the function form.

## FUNC-002: Attribute Helpers

Attribute helper functions include `ATTR` and `FIRST_ATTR`.

Attribute names and selector predicates must remain deterministic and lintable.

## FUNC-003: HTML Helpers

HTML helper functions include `INNER_HTML` and `RAW_INNER_HTML`.

Depth and minification behavior remains to be filled from the current function reference and tests.

## FUNC-004: String and Scalar Helpers

String and scalar helpers include `TRIM`, `REPLACE`, `REGEX_REPLACE`, `SUBSTR`, `COALESCE`, `LENGTH`, and `POSITION`.

Normative argument and null behavior remains to be filled from tests.

