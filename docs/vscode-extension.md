# MarkQL VS Code Extension

## Scope

The VS Code extension lives in `editors/vscode/` and provides:

- language registration for `markql`
- TextMate syntax highlighting
- language configuration for documented comments and bracket behavior
- snippets based on verified MarkQL query patterns
- CLI-backed commands for lint, run, and version
- diagnostics mapped from `./build/markql --lint --format json`

The extension does not implement:

- a custom parser
- semantic completion
- LSP
- ownership of all `.sql` files by default

## File Associations

Auto-registered extensions:

- `.markql`
- `.mql`
- `.msql`
- `.markql.sql`

Existing repository queries are often stored as `.sql`. To open those files as MarkQL in VS Code, use a workspace or user association such as:

```json
{
  "files.associations": {
    "*.sql": "sql",
    "**/docs/**/*.sql": "markql",
    "**/tests/fixtures/queries/*.sql": "markql"
  }
}
```

Keep the association scoped to MarkQL paths instead of changing all SQL files globally.

## Settings

- `markql.cliPath`: explicit CLI binary path
- `markql.enableLintOnSave`: lint MarkQL files on save

CLI discovery fallback order when `markql.cliPath` is empty:

1. workspace-local `build/markql`
2. workspace-local `build/xsql`
3. `markql` on `PATH`
4. `xsql` on `PATH`

## Commands

- `MarkQL: Lint Current File`
- `MarkQL: Run Current File`
- `MarkQL: Show Version`

`Lint Current File` uses the CLI JSON contract and maps:

- severity
- code
- message
- help
- `doc_ref`
- source span

`Run Current File` shells out to the CLI with the active query text or query file path and writes stdout/stderr to the `MarkQL` output channel.

## Build, Test, Package

Build:

```bash
cd editors/vscode
npm install
npm run build
```

Run tests:

```bash
cd editors/vscode
npm test
```

Create a VSIX:

```bash
cd editors/vscode
npm run package
```

## Notes

- The extension treats the CLI as the semantic authority.
- Diagnostic meaning is preserved from the CLI; only editor presentation is adapted.
- Untitled buffers lint via `--lint "<query>"`, while saved files lint via `--query-file`.
