# MarkQL Editor Support Plan

## Purpose

This plan defines the first editor-support milestone for MarkQL:

- a production-oriented VS Code extension
- a thin classic Vim plugin

The design stays grounded in the existing MarkQL CLI, grammar docs, and diagnostics contract. It does not introduce a second parser, validator, or semantic engine.

## Grounding Sources

Primary sources used for this plan:

- `docs/book/appendix-grammar.md`
- `docs/book/appendix-function-reference.md`
- `docs/book/appendix-operator-reference.md`
- `docs/book/ch02-mental-model.md`
- `docs/book/ch03-first-query-loop.md`
- `docs/book/ch12-troubleshooting.md`
- `docs/markql-cli-guide.md`
- `docs/grammar/MarkQLLexer.g4`
- `docs/grammar/MarkQLParser.g4`
- `core/include/markql/diagnostics.h`
- `python/markql/_meta.py`

Observed implementation contract for lint JSON:

- `./build/markql --lint "SELECT FROM doc" --format json`
- output shape: top-level `summary` plus `diagnostics[]`
- diagnostics include `severity`, `code`, `message`, `help`, `doc_ref`, and `span`
- `span` includes line/column and byte offsets

## Layout Decision

Chosen repo layout:

- `editors/vscode/`
- `editors/vim/`

Why:

- the repo already has other user-facing integrations outside core engine layout
- `editors/` keeps editor packages separate from the CLI/runtime code
- the Vim compatibility package and the VS Code extension can share docs without coupling to core modules

## Architecture Decisions

### VS Code

- Language id: `markql`
- Semantic source of truth: the existing MarkQL CLI
- Lint and run commands shell out to the configured CLI path
- Diagnostics are mapped from CLI JSON without rewriting meaning
- Syntax highlighting uses a TextMate grammar only for lexical/structural coloring
- No custom parser, no LSP, no semantic completion in v1

### Vim

- Scope is intentionally narrow: filetype detection, syntax highlighting, and a small ftplugin
- No semantic validation from Vimscript
- No indentation file unless a simple, low-risk rule set proves worthwhile

## File Association Policy

Repo evidence shows MarkQL queries are commonly stored as `.sql` files today.

v1 editor policy:

- auto-register dedicated MarkQL extensions for zero-conf usage
- do not take over every `.sql` file by default
- document manual association for existing MarkQL `.sql` files

Why:

- auto-claiming `.sql` would be intrusive for mixed-language workspaces
- the repository does not define a dedicated historical query extension today

## Included In V1

### VS Code

- language registration
- TextMate grammar for documented keywords, functions, axes, operators, strings, numbers, comments, and punctuation
- language configuration for comments, brackets, auto-closing pairs, surrounding pairs, and identifier word matching
- snippets grounded in the book and CLI docs
- commands:
  - `MarkQL: Lint Current File`
  - `MarkQL: Run Current File`
  - `MarkQL: Show Version`
- settings:
  - `markql.cliPath`
  - `markql.enableLintOnSave`
  - `markql.run.useActiveFileInput`
- diagnostics mapping from CLI JSON to VS Code diagnostics
- deterministic tests for activation logic, file association metadata, lint integration, diagnostics mapping, and run command behavior

### Vim

- filetype detection for dedicated MarkQL extensions
- readable syntax rules for documented tokens
- ftplugin with local comment settings and conservative formatting defaults
- installation and usage docs

## Explicitly Deferred

- LSP
- semantic completion
- hover docs
- go-to-definition
- rename
- Tree-sitter
- Neovim-only features
- custom parser logic in TypeScript or Vimscript
- broad `.sql` auto-detection

## CLI Integration Plan

The VS Code extension will:

1. resolve the CLI path from settings plus safe defaults
2. run `--version` for the version command
3. run `--lint --format json` for diagnostics
4. run `--query-file <active file>` for execution
5. stream stdout/stderr to a dedicated output channel
6. preserve CLI exit-code meaning:
   - `0`: no lint errors / successful execution
   - `1`: lint errors
   - `2`: CLI/tooling failure

The extension will prefer `./build/markql` in this repository context and allow explicit override with `markql.cliPath`.

## Testing Strategy

Primary test goals:

- keep tests deterministic
- avoid UI-timing fragility
- test editor integration behavior as pure logic where possible

Planned test layers:

- unit tests for CLI path resolution and diagnostics mapping
- command tests with deterministic fixture queries
- activation tests with a mocked VS Code surface
- metadata tests for language registration/package manifest
- Vim syntax smoke checks with fixture lines

## Future LSP Migration Path

This v1 keeps a clean seam for a future LSP:

- command/process execution will be isolated behind a CLI adapter
- diagnostics mapping will already consume structured data
- language metadata, snippets, and grammar can remain in place when an LSP is added
- the future LSP can replace command-triggered linting without discarding the packaging structure

## Backward Compatibility Notes

- MarkQL query syntax and semantics remain unchanged
- CLI diagnostics remain the sole semantic authority
- documented legacy forms such as alias-as-value compatibility remain editor-highlighted, not redefined
- Vim support is intentionally compatibility-first and minimal
- existing `.sql` query files continue to work with the CLI unchanged

## Initial Implementation Order

1. scaffold `editors/vscode`
2. add VS Code manifest, grammar, language configuration, snippets, and commands
3. add CLI adapter and diagnostics mapper
4. add deterministic extension tests
5. scaffold `editors/vim`
6. add filetype detection, syntax, and ftplugin
7. write installation and usage docs
8. run the smallest relevant verification set and fix failures
