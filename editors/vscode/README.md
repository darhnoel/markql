# MarkQL for VS Code

MarkQL adds focused editor support for MarkQL queries in Visual Studio Code without reimplementing the language engine. The extension uses the existing MarkQL CLI for linting and execution, so diagnostics stay aligned with the real language behavior.

## Features

- language registration for MarkQL query files
- TextMate syntax highlighting for documented MarkQL clauses, built-ins, axes, operators, strings, numbers, and comments
- snippets for common query patterns such as `PROJECT(...)`, `FLATTEN(...)`, `EXISTS(...)`, and export targets
- `MarkQL: Lint Current File`
- `MarkQL: Run Current File`
- `MarkQL: Show Version`
- diagnostics sourced from `markql --lint --format json`

## File Extensions

The extension auto-registers these extensions:

- `.mql`
- `.msql`
- `.markql`
- `.markql.sql`

It does not claim every `.sql` file by default. For existing MarkQL `.sql` files, use a scoped `files.associations` rule or manually switch the language mode to `MarkQL`.

## Settings

- `markql.cliPath`: explicit path to the MarkQL CLI
- `markql.enableLintOnSave`: run lint automatically on save

When `markql.cliPath` is empty, the extension tries:

1. workspace `build/markql`
2. workspace `build/markql.exe`
3. legacy workspace build names
4. `markql` on `PATH`
5. legacy CLI names on `PATH`

## Requirements

- Visual Studio Code 1.85 or newer
- an available MarkQL CLI for lint and run commands

## Development

Build:

```bash
cd editors/vscode
npm install
npm run build
```

Test:

```bash
cd editors/vscode
npm test
```

Package:

```bash
cd editors/vscode
npm run package
```

Publish to Marketplace:

```bash
cd editors/vscode
npx @vscode/vsce login <your-publisher-id>
npm run publish:marketplace
```

Before first public publish, replace the placeholder `publisher` value in `package.json` with your real Marketplace publisher ID.

## Support

File issues at:

- https://github.com/darhnoel/markql/issues
