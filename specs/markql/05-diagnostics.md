# Diagnostics

Status: draft skeleton

This file defines diagnostic contracts, stable code families, and message-quality requirements.

## DIAG-001: Diagnostic Stages

Diagnostics should identify the relevant stage:

- parse boundary
- row boundary
- field boundary
- sink boundary

## DIAG-002: Message Quality

Important diagnostics must answer:

- what failed
- why it matters
- what to write instead
- where to read more

## DIAG-003: Stable Fields

Machine-readable diagnostics must keep stable:

- code
- severity
- category
- message
- span
- help
- doc reference

## DIAG-004: Language-Contract Failures

Deterministic language-contract failures should map to stable `MQL-SEM-*` or `MQL-LINT-*` diagnostics.

True IO, network, and environment failures may remain generic runtime diagnostics.

## DIAG-005: Migration Diagnostics

Migration-era diagnostics should cover:

- removed `self`
- legacy tag-as-row forms
- alias-as-value ambiguity
- `SELECT alias.*` misuse
- field-boundary vs row-boundary confusion

