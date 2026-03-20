"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.registerExtension = registerExtension;
exports.lintDocument = lintDocument;
exports.runDocument = runDocument;
const cli_1 = require("./cli");
const diagnostics_1 = require("./diagnostics");
const COMMANDS = {
    lint: "markql.lintCurrentFile",
    run: "markql.runCurrentFile",
    version: "markql.showVersion"
};
function registerExtension(api, context) {
    const output = api.window.createOutputChannel("MarkQL");
    const diagnostics = api.languages.createDiagnosticCollection("markql");
    context.subscriptions.push(output, diagnostics);
    context.subscriptions.push(api.commands.registerCommand(COMMANDS.lint, async () => {
        const document = getActiveMarkqlDocument(api);
        if (!document) {
            api.window.showWarningMessage("Open a MarkQL document to lint.");
            return;
        }
        await safely(api, async () => {
            await lintDocument(api, document, diagnostics, output, true);
        });
    }));
    context.subscriptions.push(api.commands.registerCommand(COMMANDS.run, async () => {
        const document = getActiveMarkqlDocument(api);
        if (!document) {
            api.window.showWarningMessage("Open a MarkQL document to run.");
            return;
        }
        await safely(api, async () => {
            await runDocument(api, document, output);
        });
    }));
    context.subscriptions.push(api.commands.registerCommand(COMMANDS.version, async () => {
        await safely(api, async () => {
            const document = api.window.activeTextEditor?.document || null;
            const result = await runCliCommand(api, document, ["--version"], output, true);
            const version = result.stdout.trim() || result.stderr.trim();
            if (version) {
                api.window.showInformationMessage(version);
            }
        });
    }));
    context.subscriptions.push(api.workspace.onDidSaveTextDocument(async (document) => {
        if (!isMarkqlDocument(document)) {
            return;
        }
        if (!getEnableLintOnSave(api, document)) {
            return;
        }
        await safely(api, async () => {
            await lintDocument(api, document, diagnostics, output, false);
        });
    }));
    context.subscriptions.push(api.workspace.onDidCloseTextDocument((document) => {
        if (isMarkqlDocument(document)) {
            diagnostics.delete(document.uri);
        }
    }));
}
async function lintDocument(api, document, diagnosticsCollection, output, showOutput) {
    if (document?.uri?.scheme === "file" && document.isDirty) {
        await document.save();
    }
    const result = await runCliCommand(api, document, (0, cli_1.buildLintArgs)(document), output, showOutput);
    let lintResult;
    try {
        lintResult = (0, diagnostics_1.parseLintResult)(result.stdout);
    }
    catch (error) {
        diagnosticsCollection.delete(document.uri);
        api.window.showErrorMessage(error.message);
        return;
    }
    diagnosticsCollection.set(document.uri, (0, diagnostics_1.toVscodeDiagnostics)(api, lintResult));
    if (showOutput || result.exitCode === 2) {
        output.show(true);
    }
}
async function runDocument(api, document, output) {
    if (document?.uri?.scheme === "file" && document.isDirty) {
        await document.save();
    }
    const result = await runCliCommand(api, document, (0, cli_1.buildRunArgs)(document), output, true);
    output.show(true);
    if (result.exitCode === 0) {
        api.window.showInformationMessage("MarkQL command completed. See the MarkQL output channel.");
        return;
    }
    api.window.showErrorMessage("MarkQL command failed. See the MarkQL output channel.");
}
async function runCliCommand(api, document, args, output, showOutput) {
    const cliPath = getCliPath(api, document);
    const candidates = await (0, cli_1.resolveCliCandidates)(api, document, cliPath);
    const cwd = (0, cli_1.getCommandCwd)(api, document);
    const result = await (0, cli_1.executeCliWithFallback)(candidates, args, cwd);
    output.appendLine(`$ ${(0, cli_1.describeCommand)(result.cliPath, args)}`);
    if (result.stdout.trim()) {
        output.appendLine(result.stdout.trimEnd());
    }
    if (result.stderr.trim()) {
        output.appendLine(result.stderr.trimEnd());
    }
    if (showOutput) {
        output.show(true);
    }
    return result;
}
function getActiveMarkqlDocument(api) {
    const document = api.window.activeTextEditor?.document;
    if (!isMarkqlDocument(document)) {
        return null;
    }
    return document;
}
function getCliPath(api, document) {
    return api.workspace.getConfiguration("markql", document?.uri).get("cliPath", "");
}
function getEnableLintOnSave(api, document) {
    return api.workspace.getConfiguration("markql", document?.uri).get("enableLintOnSave", true);
}
function isMarkqlDocument(document) {
    return !!document && document.languageId === "markql";
}
async function safely(api, action) {
    try {
        await action();
    }
    catch (error) {
        api.window.showErrorMessage(error?.message || "MarkQL command failed.");
    }
}
