const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const packageJson = JSON.parse(
  fs.readFileSync(path.join(__dirname, "..", "package.json"), "utf8")
);
const grammarJson = JSON.parse(
  fs.readFileSync(path.join(__dirname, "..", "syntaxes", "markql.tmLanguage.json"), "utf8")
);

test("package.json registers the MarkQL language and commands", () => {
  const language = packageJson.contributes.languages[0];
  assert.equal(language.id, "markql");
  assert.deepEqual(language.extensions, [".mql", ".msql", ".markql", ".markql.sql"]);
  const commandIds = packageJson.contributes.commands.map((entry) => entry.command);
  assert.deepEqual(commandIds, [
    "markql.lintCurrentFile",
    "markql.runCurrentFile",
    "markql.showVersion"
  ]);
});

test("package.json includes Marketplace metadata", () => {
  assert.equal(packageJson.icon, "images/markql-icon.png");
  assert.equal(packageJson.license, "SEE LICENSE IN LICENSE");
  assert.equal(packageJson.pricing, "Free");
  assert.equal(packageJson.galleryBanner.color, "#143642");
  assert.equal(packageJson.categories.includes("Snippets"), true);
  assert.equal(packageJson.keywords.includes("markql"), true);
  assert.equal(packageJson.capabilities.virtualWorkspaces.supported, false);
});

test("grammar contains grounded MarkQL clauses and built-ins", () => {
  const clauses = grammarJson.repository.clauses.patterns[0].match;
  const functions = grammarJson.repository.functions.patterns[0].match;
  assert.match(clauses, /SELECT/);
  assert.match(clauses, /ORDER/);
  assert.match(functions, /PROJECT/);
  assert.match(functions, /DIRECT_TEXT/);
  assert.match(functions, /RAW_INNER_HTML/);
});
