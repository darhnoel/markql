const test = require("node:test");
const assert = require("node:assert/strict");

const { parseLintResult, toVscodeDiagnostics } = require("../out/diagnostics.js");

const sampleJson = JSON.stringify({
  summary: {
    parse_succeeded: false,
    coverage: "parse_only",
    relation_style_query: false,
    used_reduced_validation: false,
    status: "parse_failed",
    message: "Query parsing failed before full lint validation could run.",
    coverage_note: "No semantic validation ran after parsing.",
    error_count: 1,
    warning_count: 0,
    note_count: 0
  },
  diagnostics: [
    {
      severity: "ERROR",
      code: "MQL-SYN-0001",
      message: "Missing projection after SELECT",
      help: "Add a tag, self, *, or a projection expression after SELECT.",
      doc_ref: "docs/book/appendix-grammar.md",
      span: {
        start_line: 1,
        start_col: 8,
        end_line: 1,
        end_col: 9
      },
      related: []
    }
  ]
});

const api = {
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
  }
};

test("parseLintResult accepts the documented CLI lint object", () => {
  const parsed = parseLintResult(sampleJson);
  assert.equal(parsed.summary.status, "parse_failed");
  assert.equal(parsed.diagnostics[0].code, "MQL-SYN-0001");
});

test("toVscodeDiagnostics maps line and column spans to zero-based ranges", () => {
  const parsed = parseLintResult(sampleJson);
  const diagnostics = toVscodeDiagnostics(api, parsed);
  assert.equal(diagnostics.length, 1);
  assert.equal(diagnostics[0].range.start.line, 0);
  assert.equal(diagnostics[0].range.start.character, 7);
  assert.match(diagnostics[0].message, /Docs: docs\/book\/appendix-grammar\.md/);
});
