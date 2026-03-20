"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.resolveCliCandidates = resolveCliCandidates;
exports.executeCliWithFallback = executeCliWithFallback;
exports.buildLintArgs = buildLintArgs;
exports.buildRunArgs = buildRunArgs;
exports.getCommandCwd = getCommandCwd;
exports.describeCommand = describeCommand;
const node_child_process_1 = require("node:child_process");
const promises_1 = require("node:fs/promises");
const node_path_1 = require("node:path");
const PATH_CANDIDATES = ["markql", "xsql"];
const WORKSPACE_CANDIDATES = [
    ["build", "markql"],
    ["build", "xsql"]
];
async function resolveCliCandidates(api, document, explicitCliPath) {
    const trimmed = (explicitCliPath || "").trim();
    if (trimmed.length > 0) {
        return [trimmed];
    }
    const candidates = [];
    const workspaceFolder = api.workspace.getWorkspaceFolder?.(document?.uri);
    const workspacePath = workspaceFolder?.uri?.fsPath;
    if (workspacePath) {
        for (const parts of WORKSPACE_CANDIDATES) {
            const candidate = (0, node_path_1.join)(workspacePath, ...parts);
            if (await pathExists(candidate)) {
                candidates.push(candidate);
            }
        }
    }
    return candidates.concat(PATH_CANDIDATES);
}
async function executeCliWithFallback(cliCandidates, args, cwd, runner = spawnCommand) {
    let lastError;
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
        }
        catch (error) {
            if (error && error.code === "ENOENT") {
                lastError = error;
                continue;
            }
            throw error;
        }
    }
    const notFoundError = new Error("Unable to find a MarkQL CLI. Set markql.cliPath or build ./build/markql.");
    if (lastError?.code) {
        notFoundError.code = lastError.code;
    }
    throw notFoundError;
}
function buildLintArgs(document) {
    if (isBackedBySavedFile(document)) {
        return ["--lint", "--query-file", document.uri.fsPath, "--format", "json"];
    }
    return ["--lint", document.getText(), "--format", "json"];
}
function buildRunArgs(document) {
    if (isBackedBySavedFile(document)) {
        return ["--query-file", document.uri.fsPath];
    }
    return ["--query", document.getText()];
}
function getCommandCwd(api, document) {
    const workspaceFolder = api.workspace.getWorkspaceFolder?.(document?.uri);
    if (workspaceFolder?.uri?.fsPath) {
        return workspaceFolder.uri.fsPath;
    }
    if (document?.uri?.fsPath) {
        return (0, node_path_1.dirname)(document.uri.fsPath);
    }
    return ".";
}
function describeCommand(cliPath, args) {
    return [cliPath].concat(args.map(quoteArg)).join(" ");
}
async function pathExists(path) {
    try {
        await (0, promises_1.access)(path);
        return true;
    }
    catch {
        return false;
    }
}
async function spawnCommand(command, args, cwd) {
    return await new Promise((resolve, reject) => {
        const child = (0, node_child_process_1.spawn)(command, args, {
            cwd,
            shell: false,
            env: globalThis.process?.env
        });
        let stdout = "";
        let stderr = "";
        child.stdout?.on("data", (chunk) => {
            stdout += String(chunk);
        });
        child.stderr?.on("data", (chunk) => {
            stderr += String(chunk);
        });
        child.on("error", (error) => reject(error));
        child.on("close", (code) => {
            resolve({
                exitCode: code ?? 0,
                stdout,
                stderr
            });
        });
    });
}
function isBackedBySavedFile(document) {
    return document?.uri?.scheme === "file" && !document?.isDirty;
}
function quoteArg(arg) {
    if (/^[A-Za-z0-9_./:-]+$/.test(arg)) {
        return arg;
    }
    return JSON.stringify(arg);
}
