const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const { registerExtension, lintDocument, runDocument } = require("../out/registration.js");

function createApi(options = {}) {
  const state = {
    commands: new Map(),
    savedDocuments: 0,
    warnings: [],
    infos: [],
    errors: [],
    outputLines: [],
    diagnosticsSet: null,
    saveListener: null,
    closeListener: null
  };

  const output = {
    appendLine(line) {
      state.outputLines.push(line);
    },
    show() {}
  };
  const diagnostics = {
    set(uri, values) {
      state.diagnosticsSet = { uri, values };
    },
    delete() {}
  };

  const api = {
    __state: state,
    Position: class Position {
      constructor(line, character) {
        this.line = line;
        this.character = character;
      }
    },
    Range: class Range {
      constructor(start, end) {
        this.start = start;
        this.end = end;
      }
    },
    Diagnostic: class Diagnostic {
      constructor(range, message, severity) {
        this.range = range;
        this.message = message;
        this.severity = severity;
      }
    },
    DiagnosticSeverity: {
      Error: 0,
      Warning: 1,
      Information: 2
    },
    Uri: {
      parse(value) {
        return { value };
      }
    },
    Location: class Location {
      constructor(uri, range) {
        this.uri = uri;
        this.range = range;
      }
    },
    DiagnosticRelatedInformation: class DiagnosticRelatedInformation {
      constructor(location, message) {
        this.location = location;
        this.message = message;
      }
    },
    window: {
      activeTextEditor: options.activeTextEditor || null,
      createOutputChannel() {
        return output;
      },
      showWarningMessage(message) {
        state.warnings.push(message);
      },
      showInformationMessage(message) {
        state.infos.push(message);
      },
      showErrorMessage(message) {
        state.errors.push(message);
      }
    },
    languages: {
      createDiagnosticCollection() {
        return diagnostics;
      }
    },
    commands: {
      registerCommand(id, handler) {
        state.commands.set(id, handler);
        return {
          dispose() {}
        };
      }
    },
    workspace: {
      getWorkspaceFolder() {
        return { uri: { fsPath: repoRoot() } };
      },
      getConfiguration() {
        return {
          get(key, fallback) {
            if (key === "cliPath") {
              return options.cliPath || process.execPath;
            }
            if (key === "enableLintOnSave") {
              return true;
            }
            return fallback;
          }
        };
      },
      onDidSaveTextDocument(listener) {
        state.saveListener = listener;
        return { dispose() {} };
      },
      onDidCloseTextDocument(listener) {
        state.closeListener = listener;
        return { dispose() {} };
      }
    }
  };

  return api;
}

function createContext() {
  return {
    subscriptions: []
  };
}

test("registerExtension wires commands and listeners", () => {
  const api = createApi();
  const context = createContext();
  registerExtension(api, context);
  assert.equal(api.__state.commands.has("markql.lintCurrentFile"), true);
  assert.equal(api.__state.commands.has("markql.runCurrentFile"), true);
  assert.equal(api.__state.commands.has("markql.showVersion"), true);
  assert.equal(typeof api.__state.saveListener, "function");
  assert.equal(typeof api.__state.closeListener, "function");
});

test("lintDocument saves dirty file-backed documents and sets diagnostics", async () => {
  const query = "SELECT FROM doc";
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "markql-vscode-lint-"));
  const queryFile = path.join(tempDir, "broken.markql");
  const api = createApi({
    cliPath: path.join(repoRoot(), "build", "markql")
  });
  const diagnostics = {
    set(uri, values) {
      api.__state.diagnosticsSet = { uri, values };
    },
    delete() {}
  };
  const output = {
    appendLine() {},
    show() {}
  };
  const document = {
    languageId: "markql",
    isDirty: true,
    uri: {
      scheme: "file",
      fsPath: queryFile
    },
    async save() {
      fs.writeFileSync(queryFile, query);
      this.isDirty = false;
      api.__state.savedDocuments += 1;
      return true;
    },
    getText() {
      return query;
    }
  };

  await lintDocument(api, document, diagnostics, output, false);
  assert.equal(api.__state.savedDocuments, 1);
  assert.equal(api.__state.diagnosticsSet.values.length, 1);
});

test("runDocument reports success for a zero-exit CLI invocation", async () => {
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "markql-vscode-run-"));
  const queryFile = path.join(tempDir, "run.markql");
  fs.writeFileSync(
    queryFile,
    "SELECT self FROM RAW('<div>hello</div>') AS node_raw WHERE tag = 'div';\n"
  );
  const api = createApi({
    cliPath: path.join(repoRoot(), "build", "markql")
  });
  const output = {
    appendLine() {},
    show() {}
  };
  const document = {
    languageId: "markql",
    isDirty: false,
    uri: {
      scheme: "file",
      fsPath: queryFile
    }
  };

  await runDocument(api, document, output);
  assert.equal(api.__state.infos.length, 1);
});

function repoRoot() {
  return path.resolve(__dirname, "..", "..", "..");
}
