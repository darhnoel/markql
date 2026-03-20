"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.parseLintResult = parseLintResult;
exports.toVscodeDiagnostics = toVscodeDiagnostics;
function parseLintResult(stdout) {
    const trimmed = (stdout || "").trim();
    if (!trimmed) {
        throw new Error("MarkQL lint returned empty JSON output.");
    }
    const parsed = JSON.parse(trimmed);
    if (!parsed || !Array.isArray(parsed.diagnostics) || !parsed.summary) {
        throw new Error("MarkQL lint JSON output is missing summary or diagnostics.");
    }
    return parsed;
}
function toVscodeDiagnostics(api, lintResult) {
    return lintResult.diagnostics.map((entry) => {
        const range = new api.Range(new api.Position(zeroBased(entry.span?.start_line), zeroBased(entry.span?.start_col)), new api.Position(zeroBased(entry.span?.end_line), zeroBased(entry.span?.end_col)));
        const diagnostic = new api.Diagnostic(range, formatMessage(entry), mapSeverity(api, entry.severity));
        diagnostic.source = "markql";
        diagnostic.code = entry.code;
        if (Array.isArray(entry.related) && entry.related.length > 0) {
            diagnostic.relatedInformation = entry.related.map((related) => {
                const location = new api.Location(api.Uri.parse("untitled:markql-related"), new api.Range(new api.Position(zeroBased(related.span?.start_line), zeroBased(related.span?.start_col)), new api.Position(zeroBased(related.span?.end_line), zeroBased(related.span?.end_col))));
                return new api.DiagnosticRelatedInformation(location, related.message);
            });
        }
        return diagnostic;
    });
}
function formatMessage(entry) {
    const lines = [entry.message];
    if (entry.help) {
        lines.push(`Help: ${entry.help}`);
    }
    if (entry.doc_ref) {
        lines.push(`Docs: ${entry.doc_ref}`);
    }
    return lines.join("\n");
}
function mapSeverity(api, severity) {
    if (severity === "WARNING") {
        return api.DiagnosticSeverity.Warning;
    }
    if (severity === "NOTE") {
        return api.DiagnosticSeverity.Information;
    }
    return api.DiagnosticSeverity.Error;
}
function zeroBased(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric) || numeric <= 0) {
        return 0;
    }
    return numeric - 1;
}
