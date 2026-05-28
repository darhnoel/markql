---
status: accepted
---

# Spec-First MarkQL C++ Refactor

MarkQL will refactor the existing C++ implementation in place, guided by a normative `specs/markql/` language specification, instead of starting a Rust rewrite. The language surface will move toward SQL-faithful alias-based scoping: aliases are canonical, bare fields use standard lexical scoping when unambiguous, `SELECT alias.*` replaces `SELECT self` for current-node row projection, and legacy tag-as-row forms are migrated with an internal parse-aware codemod before any public migration command exists.

The decision is driven by Phase 1 fact-finding: the two-stage MarkQL model already exists in the docs and runtime, the diagnostic infrastructure is already rich enough to improve rather than replace, and the largest risk is language-contract drift rather than C++ as an implementation language. Rust remains a deferred optional memo, not the active plan.

## Considered Options

- Rewrite MarkQL in Rust now. Rejected: the discovered design gap is narrower than initially assumed, and a rewrite would delay the spec, diagnostics, migration, and public API cleanup that callers need first.
- Keep evolving C++ without a normative spec. Rejected: the confusing surface around `self`, tag-as-row projection, and `TEXT(...)` scope came from behavior accreting faster than the contract.
- Make the migration command public immediately. Rejected: migration behavior should first be proven against repository queries using an internal tool, without freezing a CLI contract too early.
- Keep both DOM backends. Rejected: maintaining libxml2 and the naive parser preserves backend drift, while the target is one DOM semantics.

## Consequences

- `specs/markql/` becomes the authoritative place for grammar, row-scope semantics, field extraction, functions, diagnostics, public API, examples, and non-goals.
- Phase 2 starts with a thin executable slice: create the spec skeleton, add narrow `SELECT alias.*` support, then build the internal source-span codemod.
- Migration uses a C++ parse-aware patcher under `tools/`, with `--check`, `--diff`, and `--write`. It rewrites only proven-safe spans and reports review-needed cases instead of guessing.
- Diagnostics work focuses on message quality and stable mappings for deterministic language-contract failures. True IO, network, and environment failures remain generic runtime diagnostics.
- The C++ refactor is boundary-first: parser, AST, validator, diagnostics, runtime, CLI, and public API boundaries are clarified before broad file-size cleanup.
- libxml2 becomes the only supported DOM backend; the naive parser is removed from normal builds.
- The public API gains a stable rectangular result view layered over `QueryResult`, with Python adapters such as `to_records()`, `to_columns()`, and optional `to_pandas()` without making NumPy or pandas core dependencies.
- Rust is documented only as deferred optional work, with reconsideration triggers defined after the C++ spec-first path lands.

The detailed Phase 1 decision ledger is recorded in [docs/notes/markql-refactor-phase-1-decision-ledger.md](../notes/markql-refactor-phase-1-decision-ledger.md).
