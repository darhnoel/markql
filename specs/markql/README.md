# MarkQL Language Specification

Status: draft skeleton

This directory is the authoritative home for MarkQL language contracts. It defines normative behavior for grammar, row scope, field extraction, functions, diagnostics, public API shape, and non-goals.

This spec is created from ADR-0009 and the Phase 1 decision ledger:

- [ADR-0009: Spec-First MarkQL C++ Refactor](../../docs/adr/0009-spec-first-markql-cpp-refactor.md)
- [Phase 1 Decision Ledger](../../docs/notes/markql-refactor-phase-1-decision-ledger.md)

## SPEC-INDEX-001: Files

- [01-grammar.md](01-grammar.md)
- [02-row-scope-and-selection.md](02-row-scope-and-selection.md)
- [03-field-extraction.md](03-field-extraction.md)
- [04-functions.md](04-functions.md)
- [05-diagnostics.md](05-diagnostics.md)
- [06-public-api.md](06-public-api.md)
- [07-non-goals.md](07-non-goals.md)
- [examples/sql-faithful.md](examples/sql-faithful.md)

## SPEC-INDEX-002: Compatibility

Unless a section explicitly marks behavior as legacy or migration-only, documented existing MarkQL behavior remains supported while the SQL-faithful surface is introduced.

## SPEC-INDEX-003: Semantic Center

MarkQL keeps row selection and field extraction separate:

- outer `WHERE` decides row survival
- field expressions decide supplier and value selection

