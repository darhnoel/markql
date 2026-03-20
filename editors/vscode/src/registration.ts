import {
  buildLintArgs,
  buildRunArgs,
  describeCommand,
  executeCliWithFallback,
  getCommandCwd,
  resolveCliCandidates
} from "./cli";
import { parseLintResult, toVscodeDiagnostics } from "./diagnostics";

const COMMANDS = {
  lint: "markql.lintCurrentFile",
  run: "markql.runCurrentFile",
  version: "markql.showVersion"
};

export function registerExtension(api: any, context: any): void {
  const output = api.window.createOutputChannel("MarkQL");
  const diagnostics = api.languages.createDiagnosticCollection("markql");
  context.subscriptions.push(output, diagnostics);

  context.subscriptions.push(
    api.commands.registerCommand(COMMANDS.lint, async () => {
      const document = getActiveMarkqlDocument(api);
      if (!document) {
        api.window.showWarningMessage("Open a MarkQL document to lint.");
        return;
      }
      await safely(api, async () => {
        await lintDocument(api, document, diagnostics, output, true);
      });
    })
  );

  context.subscriptions.push(
    api.commands.registerCommand(COMMANDS.run, async () => {
      const document = getActiveMarkqlDocument(api);
      if (!document) {
        api.window.showWarningMessage("Open a MarkQL document to run.");
        return;
      }
      await safely(api, async () => {
        await runDocument(api, document, output);
      });
    })
  );

  context.subscriptions.push(
    api.commands.registerCommand(COMMANDS.version, async () => {
      await safely(api, async () => {
        const document = api.window.activeTextEditor?.document || null;
        const result = await runCliCommand(api, document, ["--version"], output, true);
        const version = result.stdout.trim() || result.stderr.trim();
        if (version) {
          api.window.showInformationMessage(version);
        }
      });
    })
  );

  context.subscriptions.push(
    api.workspace.onDidSaveTextDocument(async (document: any) => {
      if (!isMarkqlDocument(document)) {
        return;
      }
      if (!getEnableLintOnSave(api, document)) {
        return;
      }
      await safely(api, async () => {
        await lintDocument(api, document, diagnostics, output, false);
      });
    })
  );

  context.subscriptions.push(
    api.workspace.onDidCloseTextDocument((document: any) => {
      if (isMarkqlDocument(document)) {
        diagnostics.delete(document.uri);
      }
    })
  );
}

export async function lintDocument(
  api: any,
  document: any,
  diagnosticsCollection: any,
  output: any,
  showOutput: boolean
): Promise<void> {
  if (document?.uri?.scheme === "file" && document.isDirty) {
    await document.save();
  }

  const result = await runCliCommand(api, document, buildLintArgs(document), output, showOutput);
  let lintResult: any;
  try {
    lintResult = parseLintResult(result.stdout);
  } catch (error: any) {
    diagnosticsCollection.delete(document.uri);
    api.window.showErrorMessage(error.message);
    return;
  }

  diagnosticsCollection.set(document.uri, toVscodeDiagnostics(api, lintResult));
  if (showOutput || result.exitCode === 2) {
    output.show(true);
  }
}

export async function runDocument(api: any, document: any, output: any): Promise<void> {
  if (document?.uri?.scheme === "file" && document.isDirty) {
    await document.save();
  }

  const result = await runCliCommand(api, document, buildRunArgs(document), output, true);
  output.show(true);
  if (result.exitCode === 0) {
    api.window.showInformationMessage("MarkQL command completed. See the MarkQL output channel.");
    return;
  }
  api.window.showErrorMessage("MarkQL command failed. See the MarkQL output channel.");
}

async function runCliCommand(
  api: any,
  document: any,
  args: string[],
  output: any,
  showOutput: boolean
): Promise<any> {
  const cliPath = getCliPath(api, document);
  const candidates = await resolveCliCandidates(api, document, cliPath);
  const cwd = getCommandCwd(api, document);
  const result = await executeCliWithFallback(candidates, args, cwd);

  output.appendLine(`$ ${describeCommand(result.cliPath, args)}`);
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

function getActiveMarkqlDocument(api: any): any | null {
  const document = api.window.activeTextEditor?.document;
  if (!isMarkqlDocument(document)) {
    return null;
  }
  return document;
}

function getCliPath(api: any, document: any): string {
  return api.workspace.getConfiguration("markql", document?.uri).get("cliPath", "");
}

function getEnableLintOnSave(api: any, document: any): boolean {
  return api.workspace.getConfiguration("markql", document?.uri).get("enableLintOnSave", true);
}

function isMarkqlDocument(document: any): boolean {
  return !!document && document.languageId === "markql";
}

async function safely(api: any, action: () => Promise<void>): Promise<void> {
  try {
    await action();
  } catch (error: any) {
    api.window.showErrorMessage(error?.message || "MarkQL command failed.");
  }
}
