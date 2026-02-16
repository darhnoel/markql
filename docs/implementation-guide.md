# MarkQL Implementation Guide

This guide is for contributors changing parser/runtime behavior.

## A) Project structure and boundaries
- `core/src/parser/*`: lexer + parser only. Builds AST from query text.
- `core/src/ast.h`: shared language tree structures.
- `core/src/xsql/validation.cpp`: semantic validation and guardrails.
- `core/src/executor/*`: predicate/filter/order evaluation over parsed HTML nodes.
- `core/src/xsql/execute.cpp`: query planning/execution orchestration and meta commands.
- `cli/*`: REPL and command UX only.

Dependency rules:
- Parser must not depend on executor/runtime modules.
- AST types are shared contracts; parser writes them, validator/executor read them.
- CLI must call public query APIs, not parser internals.

## B) Error handling and diagnostics
- Return parse/validation errors early.
- Include spans/positions when available.
- Keep error strings deterministic (stable wording and order).
- Prefer actionable messages users can fix immediately.

Standard format:
- First line: concise error.
- Second line (optional): `Tip: ...`.

Example:
- `Error: Unknown qualifier: foo`
- `Tip: Use self.<field> for current-row fields (for example self.node_id).`

## C) AST and semantics conventions
- Add a new AST variant when semantics differ (do not overload unrelated variants).
- Avoid catch-all/default handling for AST switches; handle variants explicitly.
- Keep parsing and semantic validation separate:
  - Parser: syntax only.
  - Validator: mode constraints, compatibility rules, qualifier checks.

## D) Performance and data movement
- Avoid copying large HTML/text buffers in hot paths.
- Prefer passing `const&` through evaluator paths.
- If copying is required, do it once and document why.
- Cache expensive per-node computations when reused in one execution pass.

## E) Testing conventions
- Parser tests:
  - syntax acceptance
  - syntax rejection
  - deterministic parse errors
- Evaluator tests:
  - semantic behavior
  - scoping/rebinding in `EXISTS(...)` + axes
- Golden tests:
  - only when output formatting stability is the goal
  - keep minimal and focused

## F) Tiny worked example checklist
When adding a language feature:
1. Tokenizer/grammar
2. AST
3. Validator/type rules
4. Evaluator semantics (including scope rebinding)
5. Tests
6. Docs
