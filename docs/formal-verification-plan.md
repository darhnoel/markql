# Lean Integration Plan for MarkQL

## Assumptions

- This plan is grounded in the current MarkQL docs and parser/diagnostics code, especially:
  - `docs/book/appendix-grammar.md`
  - `docs/markql-cli-guide.md`
  - `docs/book/ch12-troubleshooting.md`
  - `docs/book/ch02-mental-model.md`
  - `docs/book/ch04-sources-and-loading.md`
  - `docs/book/ch06-structural-row-filtering-exists.md`
  - `docs/book/ch10-null-and-stability.md`
  - `docs/book/SUMMARY.md`
  - `docs/grammar/MarkQLLexer.g4`
  - `docs/grammar/MarkQLParser.g4`
  - `core/src/lang/markql_parser.h`
  - `core/src/runtime/engine/execute_lint.cpp`
  - `core/src/lang/diagnostics.cpp`
- The current production parser is the handwritten C++ parser behind `parse_query`, not the Lean model.
- `--lint` is the existing parse + validate boundary and already has stable automation-facing behavior:
  - text and JSON diagnostics
  - stable diagnostic codes
  - exit codes `0` / `1` / `2`
- Phase 1 is intentionally an under-approximation of MarkQL, not a full language formalization.

## Implementation-Agnostic Verification Principle

- Lean specifies MarkQL the language, not C++ the current implementation.
- C++, Rust, or any future engine are backend implementations of the same language contract.
- The proof system is anchored to:
  - the formal MarkQL subset
  - theorem-backed acceptance/rejection expectations
  - a stable external lint/conformance interface
- Changing the implementation language must not require redesign of:
  - the Lean reference model
  - the theorem set
  - the machine-readable formal case corpus
  - the conceptual proof architecture
- If the engine changes, only the adapter layer should need revision, assuming the lint contract is preserved.

## A. Branch Strategy

### Proposed branch name

`lean/formal-check-foundation`

### What should happen on this branch

- Add a written plan and minimal formal-verification scaffolding.
- Keep all Lean work isolated under a dedicated directory tree, separate from any current engine build.
- Add opt-in conformance artifacts that compare Lean expectations against the current engine's lint interface.
- Keep any new scripts or tests small, readable, and subset-scoped.

### What should not happen on this branch

- No replacement of the current production parser.
- No broad grammar rewrite.
- No change to accepted MarkQL syntax or runtime semantics.
- No default-build dependency on Lean.
- No attempt to formalize the full language in one step.
- No diagnostic-message churn unless it is explicitly justified by documented conformance goals.

### Merge criteria

- The plan document is complete and reviewable.
- Any added Lean project is opt-in and isolated from the production build.
- Phase-1 conformance artifacts target only the documented tiny subset.
- The current engine's lint behavior for existing queries is unchanged.
- New artifacts are testable and deterministic with fixed query fixtures or inline query strings.

## B. Current-State Analysis

### How MarkQL currently validates queries

Current validation is staged:

1. The current engine parses query text and either produces a parsed query representation or a parse failure with message and byte position.
2. The current lint entry point turns parse failures into syntax diagnostics.
3. If parsing succeeds, lint runs validator checks and returns semantic diagnostics where needed.
4. CLI lint mode renders those diagnostics as text or JSON and exits with stable codes.

This matches the docs:

- `docs/book/appendix-grammar.md` documents the core query shape and explicitly recommends `./build/markql --lint` in the current CLI.
- `docs/markql-cli-guide.md` defines lint as syntax + key semantic validation without execution, with JSON output and exit codes.
- `docs/book/ch12-troubleshooting.md` frames failures by stage: parse, row filter, field extraction, sink.

### Existing validation and diagnostic surfaces

#### Parser

- The ANTLR grammar in `docs/grammar/MarkQLParser.g4` is a parser-level baseline.
- The current production parser is the handwritten C++ parser under `core/src/lang/parser/`.
- Parser failures already produce concrete messages such as:
  - `Expected FROM`
  - `Expected axis name (self, parent, child, ancestor, descendant)`
  - `Expected END to close CASE expression`

#### Lint

- `core/src/runtime/engine/execute_lint.cpp` is the current parse + validate entry point in the C++ engine.
- It already separates:
  - syntax failures from parse errors
  - semantic failures from validator checks
  - warnings such as ambiguous `SELECT <from_alias>`
- `docs/markql-cli-guide.md` documents JSON output and stable exit codes:
  - `0` for no `ERROR` diagnostics
  - `1` for one or more `ERROR` diagnostics
  - `2` for CLI/tooling failure

#### Troubleshooting and error messages

- `docs/book/ch12-troubleshooting.md` already teaches stage-aware diagnosis.
- `core/src/lang/diagnostics.cpp` maps parse/semantic failures into stable diagnostic codes, help text, snippets, and doc references.
- Existing diagnostics are already principled enough to serve as a comparison target.

### Why lint is the natural first Lean integration boundary

`lint` is the right first bridge because it is:

- already stable for automation
- already stage-aware
- already non-executing
- already aligned with documented grammar and troubleshooting guidance

That makes it low risk. Lean does not need to replace execution or the current parser. It only needs to provide a reference acceptance/rejection model and later, selective diagnostic expectations for a subset of queries. The stable anchor is the external lint contract, not the implementation language.

## C. Formalization Scope Proposal

### Core MarkQL Formal Subset, phase 1

The smallest useful formal subset should include only:

- `SELECT <projection> FROM doc`
- `SELECT <projection> FROM doc AS <alias>`
- `<projection>` is either:
  - `*`
  - a single identifier
- `<source>` is exactly `doc`
- optional alias after `AS`

### Explicit exclusions for phase 1

Do not include these in phase 1:

- `WHERE`
- `JOIN`
- `WITH`
- `PROJECT`
- `FLATTEN`
- `CASE`
- `ORDER BY`
- `LIMIT`
- `TO`
- `RAW(...)`
- `PARSE(...)`
- `document` synonym
- multi-item select lists
- field-qualified projections such as `a.href`

### Why this subset is the right first foothold

- It is directly grounded in the documented core query shape.
- It exercises the main structural boundary: `SELECT ... FROM ...`.
- It is enough to prove acceptance/rejection behavior and parser determinism on a real MarkQL slice.
- It avoids premature formalization of semantics that depend on row scopes, field extraction, or validator rules.

This subset is intentionally narrower than full MarkQL. That is a feature, not a bug. In phase 1, Lean should model only what can be kept obviously correct and cheap to maintain.

## D. Correctness Model

### Phase 1 correctness

In phase 1, "correctness" means:

- Lean accepts queries in the tiny subset that should parse successfully.
- Lean rejects malformed queries in that subset-adjacent space.
- The current engine's lint interface agrees with Lean on accept vs reject for those test cases.
- CLI exit code behavior agrees with the verdict class.

### Later-phase correctness

Later, correctness can expand to:

- structured parse-error family expectations
- selected semantic validation expectations
- selected semantic theorems about staged evaluation

### Required distinction

#### Lean as specification/reference model

- Lean defines a small, explicit reference grammar and later a small semantic model.
- Lean can prove theorems about that model.
- Lean does not prove a production backend correct by itself.

#### Engine backend as production implementation

- The production engine remains the real parser and source of runtime behavior.
- It must remain backward compatible.
- It should not be replaced or blocked on Lean adoption.
- Today, that engine is the C++ CLI.

#### Conformance tests as the bridge

- Conformance tests compare Lean-derived expectations with the engine's external lint contract.
- They establish empirical agreement on a known subset.
- They are the practical mechanism that connects proofs to the existing system.

This is the key engineering posture: prove the model, then test production against it.

### Engine Conformance Contract

Any engine backend used for phase-1 conformance must satisfy this external contract:

- accept a query string as input
- expose a non-executing lint or validation mode
- return machine-readable diagnostics
- preserve stable verdict classes:
  - accepted
  - rejected with syntax error
  - rejected with semantic or validation error
- preserve automation-stable exit behavior
- keep query-language behavior independent of implementation language

This contract is intentionally external. It does not constrain:

- internal AST shape
- internal parser architecture
- internal diagnostic data structures
- implementation language

For the current codebase, the C++ CLI satisfies this contract via `./build/markql --lint --format json`.

## E. First Lean Milestone

### Goal

Create the smallest possible Lean 4 project that can:

- encode a tiny MarkQL AST
- parse a tiny token stream for the core subset
- prove a handful of acceptance/rejection examples

### Minimal project layout

```text
formal/lean/
  lean-toolchain
  lakefile.lean
  MarkQLCore/
    Syntax.lean
    Lexer.lean
    Parser.lean
    Examples.lean
    Theorems.lean
```

### Tiny AST

Use a very small AST:

```text
Projection := star | ident name
Source := doc
Query := select projection alias?
```

Alias should be modeled as an optional identifier, with a separate well-formedness rule that rejects reserved alias `self`, matching the documented restriction that `self` is reserved in query grammar and cannot be used as a source alias.

### Tiny parser shape

Do not use a general parser-combinator framework in phase 1.

Use:

- a tiny token type
- a tiny lexer for just:
  - `SELECT`
  - `FROM`
  - `AS`
  - `doc`
  - `*`
  - identifier
- a direct recursive-descent or token-consumption parser for one grammar:

```text
query := SELECT projection FROM doc (AS identifier)? EOF
```

### Acceptance/rejection theorems

Target 5 to 10 theorem-backed examples:

Accepted examples:

- `SELECT * FROM doc`
- `SELECT div FROM doc`
- `SELECT div FROM doc AS node_div`
- `SELECT title FROM doc`
- `SELECT section FROM doc AS node_section`

Rejected examples:

- `SELECT FROM doc`
- `SELECT div doc`
- `SELECT div FROM`
- `FROM doc SELECT div`
- `SELECT div FROM doc AS self`
- `SELECT div FROM document`
- `SELECT div, span FROM doc`

### Why these examples

- They cover missing projection, missing `FROM`, missing source, wrong clause order, reserved alias, unsupported synonym in the subset, and unsupported multi-projection.
- They remain small enough to prove directly without parser-framework overhead.

### Effort and risk notes

- Effort: low to moderate if kept strictly subset-scoped.
- Risk: low if the parser stays tiny and theorem count stays small.
- Main risk: accidental over-design. The milestone must stop once acceptance/rejection proofs exist and compile.

## F. Integration Strategy with the Current Engine

### First bridge: lint conformance

Use the current engine's lint interface as the comparison target.

Initial conformance flow:

1. Pick a test case that is inside the Lean subset.
2. Record the Lean-derived expectation:
   - accept
   - reject
3. Run the conformance harness through an engine adapter.
4. Compare:
   - whether diagnostics contain an `ERROR`
   - whether exit code is `0` or `1`

Today, the adapter can invoke `./build/markql --lint --format json`. That is a backend realization of the contract, not the long-term architectural anchor.

### Adapter abstraction

The conformance harness should target an adapter interface, not a hardcoded C++ command.

Conceptually:

- Lean theorem set and formal case corpus
- conformance runner
- engine adapter
- engine backend

Responsibilities:

- Lean theorem set and formal case corpus
  - define language-level expectations for the tiny subset
- conformance runner
  - load machine-readable cases and compare expected versus observed verdicts
- engine adapter
  - translate the runner's request into the current backend's lint invocation
  - normalize returned exit status and diagnostic family into the stable contract
- engine backend
  - current example: C++ CLI
  - future possibility: Rust CLI

The adapter layer is the only component that should care about command paths, wrapper scripts, or small JSON-shape normalization issues.

### Conformance harness design

Recommended harness behavior:

- input: a list of subset test cases with:
  - query text
  - expected verdict
  - optional expected class label
- scope rule:
  - only queries intentionally inside the Lean subset are compared
  - anything outside the phase-1 subset is out of scope and skipped, not treated as a failure of Lean coverage
- execution:
  - call the configured engine adapter
- assertions in phase 1:
  - `accept` => exit code `0`
  - `reject` => exit code `1`

The harness should not compare full message text in phase 1.

### Why verdict-only first

- Diagnostic wording is more volatile than acceptance/rejection.
- The subset model will be ahead of any polished diagnostic taxonomy.
- Verdict-only comparison creates useful signal without over-claiming proof.

### Later harness extensions

Later phases may compare:

- syntax vs semantic family
- stable diagnostic code class
- message category such as:
  - missing clause
  - expected token
  - reserved alias

## G. Guidance and Diagnostics Strategy

### What Lean can realistically improve

Over time, Lean can improve diagnostics by forcing the project to define:

- explicit expected-token sets for subset grammar states
- explicit clause-order expectations
- explicit stage boundaries for semantic reasoning

That can improve user guidance in three ways.

### 1. More principled expected-token and expected-clause diagnostics

For a formal subset, Lean can make it precise that after:

- `SELECT` a projection must follow
- projection, `FROM` must follow
- `FROM`, source must follow
- `AS`, identifier must follow

That can guide a cleaner diagnostic taxonomy in any backend, even if the current C++ parser remains handwritten.

### 2. Better stage-aware messaging

The docs already teach staged reasoning:

- parse boundary
- row survival boundary
- field extraction boundary
- sink boundary

Lean can help by keeping the specification explicit about which failures belong to grammar and which belong to later validation. That should reduce mixing parse errors with semantic guidance.

### 3. Clearer parse vs semantic separation

The current lint pipeline already separates parse and semantic validation. Lean can reinforce that split by:

- formally modeling parse acceptance first
- then adding a separate semantic relation later

This mirrors MarkQL's staged mental model and existing troubleshooting guidance.

### What Lean cannot promise

- It cannot prove the current full diagnostic wording optimal.
- It cannot prove all current-engine diagnostics correct without much broader formalization.
- It cannot by itself guarantee every non-subset parser path has perfect guidance.

## H. Risks and Non-Goals

### Main risks

- Proving only the Lean model, not a backend implementation.
- Drift between the Lean subset and the evolving grammar/docs.
- Expanding scope too early into joins, extraction, or sink behavior.
- Adding maintenance burden if the subset grows faster than its value.
- Creating a false sense of global proof coverage from a tiny subset.

### Non-goals for phase 1

- Full MarkQL grammar formalization.
- Replacing ANTLR or the current handwritten parser.
- Proving runtime execution correctness.
- Formalizing DOM semantics.
- Formalizing `WHERE`, `EXISTS`, `TEXT`, `ATTR`, `PROJECT`, `FLATTEN`, or sink semantics.
- Matching full diagnostic text.
- Making Lean mandatory for all contributors before the workflow proves value.
- Proving one specific C++ parser implementation forever.
- Binding proofs to internal engine data structures.
- Depending on exact diagnostic wording.
- Depending on a single executable path layout.

## I. Concrete File Plan

Create these files and directories first:

```text
docs/
  formal-verification-plan.md

formal/lean/
  lean-toolchain
  lakefile.lean
  README.md
  MarkQLCore/
    Syntax.lean
    Lexer.lean
    Parser.lean
    Examples.lean
    Theorems.lean

tests/formal_conformance/
  README.md
  core_select_doc_alias_cases.json

scripts/
  run_formal_conformance.sh
  engine_adapters/
    cli_json_adapter.sh
```

### File intent

- `docs/formal-verification-plan.md`
  - architectural and rollout plan
- `formal/lean/README.md`
  - scope and how to run the tiny Lean project
- `formal/lean/MarkQLCore/*.lean`
  - tiny reference model and proofs
- `tests/formal_conformance/core_select_doc_alias_cases.json`
  - engine-independent query cases derived from the Lean subset
- `scripts/run_formal_conformance.sh`
  - opt-in conformance runner over the stable adapter contract
- `scripts/engine_adapters/cli_json_adapter.sh`
  - current CLI-backed adapter that realizes the engine contract
  - may initially wrap `./build/markql --lint --format json`

## J. Validation Plan

### Phase 1 success conditions

Phase 1 is successful when all of the following are true:

- The Lean files compile.
- The theorem-backed examples pass.
- The conformance harness runs on the tiny subset cases.
- Subset-valid documented examples are accepted through the engine adapter.
- Subset-invalid examples are rejected through the engine adapter.
- Exit code behavior matches expectations.
- No production parser behavior changes are required.
- No existing MarkQL query behavior changes.
- Lean artifacts depend only on the formal subset, not parser internals.
- The machine-readable case corpus is engine-independent.
- The conformance harness consumes a stable adapter contract.
- Replacing the backend from the current C++ CLI to a Rust CLI would require only adapter replacement if the lint contract is preserved.

### Concrete acceptance criteria for phase 1

- Branch exists: `lean/formal-check-foundation`.
- A Lean 4 project exists under `formal/lean/`.
- The Lean subset is limited to:
  - `SELECT <identifier|*> FROM doc [AS <alias>]`
- At least 5 accepted and 5 rejected examples are encoded in Lean.
- A machine-readable, engine-independent case list exists for conformance.
- An opt-in script can run those cases through an engine adapter.
- The script checks verdict and exit code only.
- Existing engine behavior remains unchanged.

## L. Backend Replacement Scenario

### If the core engine later moves to Rust

What stays unchanged:

- Lean reference model
- theorem set
- machine-readable formal case corpus
- conceptual proof architecture
- language-level correctness definitions
- phase roadmap
- conformance methodology

What may change:

- adapter implementation
- command path
- JSON normalization details inside the adapter

What should not change:

- the formal subset for phase 1
- the meaning of accepted versus rejected cases
- the distinction between syntax rejection and semantic or validation rejection
- the requirement that conformance remain driven by an external lint contract

This is the intended portability boundary: backend replacement is an adapter concern, not a proof redesign.

## M. Next Phases

### Phase 1: Tiny grammar core

- Formalize `SELECT <identifier|*> FROM doc [AS <alias>]`
- Prove acceptance/rejection examples
- Add lint conformance harness

### Phase 2: Optional `WHERE` grammar shell

- Extend grammar subset to:
  - `SELECT ... FROM doc [AS alias] WHERE <very small predicate>`
- Keep predicate scope intentionally narrow:
  - identifier comparisons only if directly grounded in docs and current parser
- Continue verdict-only conformance

### Phase 3: `EXISTS` axis subset

- Formalize a tiny structural predicate subset:
  - `EXISTS(child WHERE ...)`
  - `EXISTS(descendant WHERE ...)`
- Focus on parse shape first, not DOM proof completeness
- Use documented axis failures like `Expected axis name` as guidance for error categories

### Phase 4: Two-stage semantics

- Introduce a very small semantic model reflecting the documented mental model:
  - outer `WHERE` controls row survival
  - field extraction computes values after row survival
- Keep the model abstract over rows/nodes at first

### Phase 5: Selected NULL-stability semantics

- Formalize narrow theorems grounded in `docs/book/ch10-null-and-stability.md`
- Example direction:
  - missing supplier implies `NULL`, not row deletion
  - row removal requires outer-stage gating

Only move to later phases after phase 1 and phase 2 are stable and cheap to maintain.

## Recommended First Commit Sequence

1. Create the exploration branch and land this plan document.
2. Add the isolated Lean 4 project skeleton under `formal/lean/`.
3. Add the tiny AST, lexer, parser, and the first theorem-backed examples.
4. Add a machine-readable subset case file.
5. Add an opt-in `scripts/run_formal_conformance.sh` plus one engine adapter that compares verdict and exit code through the lint contract.
6. Add lightweight CI only after the local workflow is stable.

## What Should Be Deferred Until Later

- Any attempt to model full projection syntax.
- Any attempt to formalize `PROJECT`, `FLATTEN`, sinks, or `ORDER BY`.
- Semantic proofs over real DOM behavior.
- Full diagnostic text matching.
- Tight CI gating on Lean before the subset and harness have shown stable value.
- Any parser refactor motivated only by the existence of Lean.

## Why This Approach Fits MarkQL Specifically

- MarkQL already has a documented staged mental model, so a staged formalization matches the language design.
- MarkQL already exposes `--lint` as a non-executing parse + validate boundary, which is the right place to attach conformance checks.
- MarkQL already values deterministic inputs and small reproducible fixtures, which fits formal and conformance testing well.
- MarkQL already distinguishes parse failures from later semantic failures in docs and code, which makes incremental formalization practical.
- MarkQL is production-oriented, so a conservative reference-model strategy is safer than trying to replace the parser.

## Note: Why This Plan Survives a C++ to Rust Engine Switch

- Lean is tied to the MarkQL subset and its proofs, not to C++ internals.
- The machine-readable case corpus encodes language-level expectations, not backend structure.
- The conformance runner talks to an adapter contract, not a fixed executable layout.
- A Rust rewrite can preserve the same external lint contract and therefore reuse the same proofs, cases, and roadmap.
