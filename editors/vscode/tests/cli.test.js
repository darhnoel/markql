const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const {
  buildLintArgs,
  buildRunArgs,
  executeCliWithFallback,
  resolveCliCandidates
} = require("../out/cli.js");

test("resolveCliCandidates prefers explicit cliPath", async () => {
  const api = {
    workspace: {
      getWorkspaceFolder() {
        return null;
      }
    }
  };
  const candidates = await resolveCliCandidates(api, null, "/tmp/markql");
  assert.deepEqual(candidates, ["/tmp/markql"]);
});

test("resolveCliCandidates includes workspace build binary when present", async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "markql-vscode-"));
  const buildDir = path.join(root, "build");
  fs.mkdirSync(buildDir, { recursive: true });
  fs.writeFileSync(path.join(buildDir, "markql"), "");

  const api = {
    workspace: {
      getWorkspaceFolder() {
        return { uri: { fsPath: root } };
      }
    }
  };
  const candidates = await resolveCliCandidates(api, { uri: { fsPath: path.join(root, "q.markql") } });
  assert.equal(candidates[0], path.join(root, "build", "markql"));
  assert.equal(candidates.includes("markql"), true);
});

test("resolveCliCandidates includes Windows workspace binaries when present", async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "markql-vscode-"));
  const buildDir = path.join(root, "build");
  fs.mkdirSync(buildDir, { recursive: true });
  fs.writeFileSync(path.join(buildDir, "markql.exe"), "");

  const api = {
    workspace: {
      getWorkspaceFolder() {
        return { uri: { fsPath: root } };
      }
    }
  };
  const candidates = await resolveCliCandidates(api, { uri: { fsPath: path.join(root, "q.markql") } });
  assert.equal(candidates[0], path.join(root, "build", "markql.exe"));
  assert.equal(candidates.includes("markql"), true);
});

test("executeCliWithFallback skips ENOENT and uses the next candidate", async () => {
  const seen = [];
  const result = await executeCliWithFallback(
    ["missing-markql", "markql"],
    ["--version"],
    "/tmp",
    async (command) => {
      seen.push(command);
      if (command === "missing-markql") {
        const error = new Error("missing");
        error.code = "ENOENT";
        throw error;
      }
      return {
        exitCode: 0,
        stdout: "markql 1.21.0\n",
        stderr: ""
      };
    }
  );
  assert.deepEqual(seen, ["missing-markql", "markql"]);
  assert.equal(result.cliPath, "markql");
});

test("buildLintArgs uses query-file for saved files", () => {
  const args = buildLintArgs({
    uri: { scheme: "file", fsPath: "/tmp/query.markql" },
    isDirty: false
  });
  assert.deepEqual(args, ["--lint", "--query-file", "/tmp/query.markql", "--format", "json"]);
});

test("buildRunArgs uses query text for unsaved documents", () => {
  const args = buildRunArgs({
    uri: { scheme: "untitled" },
    isDirty: true,
    getText() {
      return "SELECT self FROM doc;";
    }
  });
  assert.deepEqual(args, ["--query", "SELECT self FROM doc;"]);
});
