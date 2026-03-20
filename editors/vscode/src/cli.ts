import { spawn } from "node:child_process";
import { access } from "node:fs/promises";
import { dirname, join } from "node:path";

export type CliResult = {
  cliPath: string;
  args: string[];
  cwd: string;
  exitCode: number;
  stdout: string;
  stderr: string;
};

type SpawnRunner = (command: string, args: string[], cwd: string) => Promise<{
  exitCode: number;
  stdout: string;
  stderr: string;
}>;

const PATH_CANDIDATES = ["markql", "xsql"];
const WORKSPACE_CANDIDATES = [
  ["build", "markql"],
  ["build", "xsql"]
];

export async function resolveCliCandidates(
  api: any,
  document: any,
  explicitCliPath?: string
): Promise<string[]> {
  const trimmed = (explicitCliPath || "").trim();
  if (trimmed.length > 0) {
    return [trimmed];
  }

  const candidates: string[] = [];
  const workspaceFolder = api.workspace.getWorkspaceFolder?.(document?.uri);
  const workspacePath = workspaceFolder?.uri?.fsPath;
  if (workspacePath) {
    for (const parts of WORKSPACE_CANDIDATES) {
      const candidate = join(workspacePath, ...parts);
      if (await pathExists(candidate)) {
        candidates.push(candidate);
      }
    }
  }

  return candidates.concat(PATH_CANDIDATES);
}

export async function executeCliWithFallback(
  cliCandidates: string[],
  args: string[],
  cwd: string,
  runner: SpawnRunner = spawnCommand
): Promise<CliResult> {
  let lastError: any;
  for (const cliPath of cliCandidates) {
    try {
      const result = await runner(cliPath, args, cwd);
      return {
        cliPath,
        args,
        cwd,
        exitCode: result.exitCode,
        stdout: result.stdout,
        stderr: result.stderr
      };
    } catch (error: any) {
      if (error && error.code === "ENOENT") {
        lastError = error;
        continue;
      }
      throw error;
    }
  }

  const notFoundError = new Error(
    "Unable to find a MarkQL CLI. Set markql.cliPath or build ./build/markql."
  ) as any;
  if (lastError?.code) {
    notFoundError.code = lastError.code;
  }
  throw notFoundError;
}

export function buildLintArgs(document: any): string[] {
  if (isBackedBySavedFile(document)) {
    return ["--lint", "--query-file", document.uri.fsPath, "--format", "json"];
  }
  return ["--lint", document.getText(), "--format", "json"];
}

export function buildRunArgs(document: any): string[] {
  if (isBackedBySavedFile(document)) {
    return ["--query-file", document.uri.fsPath];
  }
  return ["--query", document.getText()];
}

export function getCommandCwd(api: any, document: any): string {
  const workspaceFolder = api.workspace.getWorkspaceFolder?.(document?.uri);
  if (workspaceFolder?.uri?.fsPath) {
    return workspaceFolder.uri.fsPath;
  }
  if (document?.uri?.fsPath) {
    return dirname(document.uri.fsPath);
  }
  return ".";
}

export function describeCommand(cliPath: string, args: string[]): string {
  return [cliPath].concat(args.map(quoteArg)).join(" ");
}

async function pathExists(path: string): Promise<boolean> {
  try {
    await access(path);
    return true;
  } catch {
    return false;
  }
}

async function spawnCommand(command: string, args: string[], cwd: string) {
  return await new Promise<{ exitCode: number; stdout: string; stderr: string }>((resolve, reject) => {
    const child = spawn(command, args, {
      cwd,
      shell: false,
      env: (globalThis as any).process?.env
    });

    let stdout = "";
    let stderr = "";
    child.stdout?.on("data", (chunk: any) => {
      stdout += String(chunk);
    });
    child.stderr?.on("data", (chunk: any) => {
      stderr += String(chunk);
    });
    child.on("error", (error: any) => reject(error));
    child.on("close", (code: number | null) => {
      resolve({
        exitCode: code ?? 0,
        stdout,
        stderr
      });
    });
  });
}

function isBackedBySavedFile(document: any): boolean {
  return document?.uri?.scheme === "file" && !document?.isDirty;
}

function quoteArg(arg: string): string {
  if (/^[A-Za-z0-9_./:-]+$/.test(arg)) {
    return arg;
  }
  return JSON.stringify(arg);
}
