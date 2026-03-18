const AGENT_URL = "http://127.0.0.1:7337/v1/query";
const STORAGE_KEY_TOKEN = "markqlAgentToken";
const STORAGE_KEY_QUERY = "markqlLastQuery";
const STORAGE_KEY_SNAPSHOT = "markqlSnapshotHtml";
const STORAGE_KEY_SNAPSHOT_SCOPE = "markqlSnapshotScope";
const STORAGE_KEY_LINT = "markqlLintEnabled";
const LEGACY_STORAGE_KEY_TOKEN = "xsqlAgentToken";
const LEGACY_STORAGE_KEY_QUERY = "xsqlLastQuery";
const LEGACY_STORAGE_KEY_SNAPSHOT = "xsqlSnapshotHtml";
const PRIMARY_CAPTURE_SCOPE = "full";
const FALLBACK_CAPTURE_SCOPE = "main";

let snapshotHtml = "";
let snapshotScope = "";
let lastResult = null;
let lastErrorText = "";
let isComposingQuery = false;
let lintEnabled = true;
let activeOutputTab = "table";

const SQL_KEYWORDS = new Set([
  "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "IN", "AS", "LIMIT",
  "ORDER", "BY", "ASC", "DESC", "EXISTS", "IS", "NULL", "CONTAINS",
  "ANY", "ALL", "TO", "TABLE", "LIST", "RAW", "FRAGMENTS", "ON", "OFF"
]);
const SQL_FUNCTIONS = new Set([
  "TEXT", "INNER_HTML", "RAW_INNER_HTML", "FLATTEN", "FLATTEN_TEXT",
  "PROJECT", "FLATTEN_EXTRACT", "COALESCE", "ATTR", "COUNT", "SUMMARIZE",
  "TFIDF", "TRIM", "HAS_DIRECT_TEXT"
]);

const ui = {
  tokenCompact: document.getElementById("tokenCompact"),
  tokenEditor: document.getElementById("tokenEditor"),
  tokenInput: document.getElementById("tokenInput"),
  saveTokenBtn: document.getElementById("saveTokenBtn"),
  cancelTokenBtn: document.getElementById("cancelTokenBtn"),
  editTokenBtn: document.getElementById("editTokenBtn"),
  tokenHelp: document.getElementById("tokenHelp"),
  runBtn: document.getElementById("runBtn"),
  captureBtn: document.getElementById("captureBtn"),
  examplesSelect: document.getElementById("examplesSelect"),
  formatBtn: document.getElementById("formatBtn"),
  lintBtn: document.getElementById("lintBtn"),
  queryInput: document.getElementById("queryInput"),
  sqlEditor: document.querySelector(".sql-editor"),
  lineNumbers: document.getElementById("lineNumbers"),
  copyQueryBtn: document.getElementById("copyQueryBtn"),
  maxRowsInput: document.getElementById("maxRowsInput"),
  timeoutInput: document.getElementById("timeoutInput"),
  copyExportBtn: document.getElementById("copyExportBtn"),
  statusLine: document.getElementById("statusLine"),
  resultsHead: document.querySelector("#resultsTable thead"),
  resultsBody: document.querySelector("#resultsTable tbody"),
  jsonOutput: document.getElementById("jsonOutput"),
  errorsOutput: document.getElementById("errorsOutput"),
  tabButtons: Array.from(document.querySelectorAll(".tab-button")),
  tabPanes: Array.from(document.querySelectorAll(".tab-pane"))
};

function escapeHtml(text) {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function highlightSql(query) {
  let i = 0;
  const out = [];

  const pushToken = (cls, text) => {
    out.push(`<span class="${cls}">${escapeHtml(text)}</span>`);
  };

  while (i < query.length) {
    const ch = query[i];

    if (ch === "-" && query[i + 1] === "-") {
      let j = i + 2;
      while (j < query.length && query[j] !== "\n") j += 1;
      pushToken("sql-comment", query.slice(i, j));
      i = j;
      continue;
    }

    if (ch === "/" && query[i + 1] === "*") {
      let j = i + 2;
      while (j + 1 < query.length && !(query[j] === "*" && query[j + 1] === "/")) j += 1;
      j = Math.min(query.length, j + 2);
      pushToken("sql-comment", query.slice(i, j));
      i = j;
      continue;
    }

    if (ch === "'") {
      let j = i + 1;
      while (j < query.length) {
        if (query[j] === "'") {
          if (query[j + 1] === "'") {
            j += 2;
            continue;
          }
          j += 1;
          break;
        }
        j += 1;
      }
      pushToken("sql-str", query.slice(i, j));
      i = j;
      continue;
    }

    if (/\s/.test(ch)) {
      out.push(escapeHtml(ch));
      i += 1;
      continue;
    }

    if ("(),.;".includes(ch)) {
      pushToken("sql-op", ch);
      i += 1;
      continue;
    }

    if (/[<>~=:+\-*/]/.test(ch)) {
      let j = i + 1;
      while (j < query.length && /[<>~=:+\-*/]/.test(query[j])) j += 1;
      pushToken("sql-op", query.slice(i, j));
      i = j;
      continue;
    }

    if (/[0-9]/.test(ch)) {
      let j = i + 1;
      while (j < query.length && /[0-9.]/.test(query[j])) j += 1;
      pushToken("sql-num", query.slice(i, j));
      i = j;
      continue;
    }

    if (/[A-Za-z_]/.test(ch)) {
      let j = i + 1;
      while (j < query.length && /[A-Za-z0-9_]/.test(query[j])) j += 1;
      const word = query.slice(i, j);
      const upper = word.toUpperCase();
      if (SQL_KEYWORDS.has(upper)) {
        pushToken("sql-kw", word);
      } else if (SQL_FUNCTIONS.has(upper)) {
        pushToken("sql-fn", word);
      } else {
        out.push(escapeHtml(word));
      }
      i = j;
      continue;
    }

    out.push(escapeHtml(ch));
    i += 1;
  }

  return out.join("");
}

function getQueryText() {
  return ui.queryInput ? ui.queryInput.textContent || "" : "";
}

function setQueryText(text) {
  ui.queryInput.textContent = text || "";
}

function getCaretOffset(container) {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) return 0;
  const range = selection.getRangeAt(0);
  if (!container.contains(range.endContainer)) return getQueryText().length;
  const before = range.cloneRange();
  before.selectNodeContents(container);
  before.setEnd(range.endContainer, range.endOffset);
  return before.toString().length;
}

function setCaretOffset(container, offset) {
  const selection = window.getSelection();
  if (!selection) return;

  const range = document.createRange();
  const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
  let remaining = Math.max(0, offset);
  let node = walker.nextNode();

  while (node) {
    const length = node.nodeValue.length;
    if (remaining <= length) {
      range.setStart(node, remaining);
      range.collapse(true);
      selection.removeAllRanges();
      selection.addRange(range);
      return;
    }
    remaining -= length;
    node = walker.nextNode();
  }

  range.selectNodeContents(container);
  range.collapse(false);
  selection.removeAllRanges();
  selection.addRange(range);
}

function renderLineNumbers() {
  const query = getQueryText();
  const count = Math.max(1, query.split("\n").length);
  const numbers = [];
  for (let i = 1; i <= count; i += 1) {
    numbers.push(String(i));
  }
  ui.lineNumbers.textContent = numbers.join("\n");
}

function syncEditorScroll() {
  ui.lineNumbers.scrollTop = ui.queryInput.scrollTop;
}

function renderQueryHighlight(preserveCaret = false) {
  if (!ui.queryInput || isComposingQuery) return;
  const query = getQueryText();
  const scrollTop = ui.queryInput.scrollTop;
  const caret = preserveCaret ? getCaretOffset(ui.queryInput) : 0;
  ui.queryInput.innerHTML = highlightSql(query);
  if (!query) {
    ui.queryInput.innerHTML = "";
  }
  renderLineNumbers();
  if (preserveCaret) {
    setCaretOffset(ui.queryInput, caret);
  }
  ui.queryInput.scrollTop = scrollTop;
  syncEditorScroll();
}

function insertAtCaret(text) {
  const selection = window.getSelection();
  if (!selection || selection.rangeCount === 0) {
    ui.queryInput.append(document.createTextNode(text));
    return;
  }
  const range = selection.getRangeAt(0);
  range.deleteContents();
  const node = document.createTextNode(text);
  range.insertNode(node);
  range.setStartAfter(node);
  range.collapse(true);
  selection.removeAllRanges();
  selection.addRange(range);
}

function focusQueryInput(placeCaretAtEnd = false) {
  ui.queryInput.focus();
  if (placeCaretAtEnd) {
    setCaretOffset(ui.queryInput, getQueryText().length);
  }
}

function status(text) {
  ui.statusLine.textContent = text;
}

function normalizeNumber(value, fallback, min, max) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) return fallback;
  return Math.min(max, Math.max(min, Math.floor(parsed)));
}

function valueToText(value) {
  if (value === null || value === undefined) return "";
  if (typeof value === "object") return JSON.stringify(value);
  return String(value);
}

function csvEscape(value) {
  const text = valueToText(value);
  if (text.includes('"') || text.includes(",") || text.includes("\n") || text.includes("\r")) {
    return '"' + text.replace(/"/g, '""') + '"';
  }
  return text;
}

function buildCsv(columns, rows) {
  const header = columns.map((c) => csvEscape(c.name));
  const lines = [header.join(",")];
  for (const row of rows) {
    lines.push(row.map((value) => csvEscape(value)).join(","));
  }
  return lines.join("\n");
}

function buildJson(columns, rows) {
  const objects = rows.map((row) => {
    const obj = {};
    for (let i = 0; i < columns.length; i += 1) {
      obj[columns[i].name] = i < row.length ? row[i] : null;
    }
    return obj;
  });
  return JSON.stringify(objects, null, 2);
}

function formatQueryText(query) {
  let formatted = query.replace(/\r\n?/g, "\n").trim();
  if (!formatted) return "";

  for (const keyword of SQL_KEYWORDS) {
    const pattern = new RegExp(`\\b${keyword}\\b`, "gi");
    formatted = formatted.replace(pattern, keyword);
  }
  for (const fn of SQL_FUNCTIONS) {
    const pattern = new RegExp(`\\b${fn}\\b`, "gi");
    formatted = formatted.replace(pattern, fn);
  }

  formatted = formatted
    .replace(/[ \t]+\n/g, "\n")
    .replace(/\n[ \t]+/g, "\n")
    .replace(/[ \t]{2,}/g, " ");

  const breakKeywords = ["FROM", "WHERE", "LIMIT", "TO"];
  for (const keyword of breakKeywords) {
    const pattern = new RegExp(`\\s+${keyword}\\b`, "gi");
    formatted = formatted.replace(pattern, `\n${keyword}`);
  }
  formatted = formatted.replace(/\s+ORDER\s+BY\b/gi, "\nORDER BY");
  formatted = formatted.replace(/\s+(AND|OR)\b/gi, "\n  $1");
  formatted = formatted.replace(/\n{3,}/g, "\n\n");

  if (!/;\s*$/.test(formatted)) {
    formatted += ";";
  }
  return formatted;
}

function unterminatedSingleQuote(query) {
  let inCommentLine = false;
  let inCommentBlock = false;

  for (let i = 0; i < query.length; i += 1) {
    const ch = query[i];
    const next = query[i + 1];

    if (inCommentLine) {
      if (ch === "\n") inCommentLine = false;
      continue;
    }
    if (inCommentBlock) {
      if (ch === "*" && next === "/") {
        inCommentBlock = false;
        i += 1;
      }
      continue;
    }
    if (ch === "-" && next === "-") {
      inCommentLine = true;
      i += 1;
      continue;
    }
    if (ch === "/" && next === "*") {
      inCommentBlock = true;
      i += 1;
      continue;
    }
    if (ch === "'") {
      i += 1;
      while (i < query.length) {
        if (query[i] === "'") {
          if (query[i + 1] === "'") {
            i += 2;
            continue;
          }
          break;
        }
        i += 1;
      }
      if (i >= query.length) {
        return true;
      }
    }
  }

  return false;
}

function hasBalancedParens(query) {
  let depth = 0;
  let inString = false;
  let inCommentLine = false;
  let inCommentBlock = false;

  for (let i = 0; i < query.length; i += 1) {
    const ch = query[i];
    const next = query[i + 1];

    if (inCommentLine) {
      if (ch === "\n") inCommentLine = false;
      continue;
    }
    if (inCommentBlock) {
      if (ch === "*" && next === "/") {
        inCommentBlock = false;
        i += 1;
      }
      continue;
    }
    if (inString) {
      if (ch === "'") {
        if (next === "'") {
          i += 1;
        } else {
          inString = false;
        }
      }
      continue;
    }
    if (ch === "-" && next === "-") {
      inCommentLine = true;
      i += 1;
      continue;
    }
    if (ch === "/" && next === "*") {
      inCommentBlock = true;
      i += 1;
      continue;
    }
    if (ch === "'") {
      inString = true;
      continue;
    }
    if (ch === "(") depth += 1;
    if (ch === ")") depth -= 1;
    if (depth < 0) return false;
  }

  return depth === 0;
}

function lintQuery(query) {
  const messages = [];
  const trimmed = query.trim();

  if (!trimmed) {
    messages.push({ level: "error", text: "Query is empty." });
    return messages;
  }
  if (!/\bselect\b/i.test(trimmed)) {
    messages.push({ level: "error", text: "Missing SELECT clause." });
  }
  if (!/\bfrom\b/i.test(trimmed)) {
    messages.push({ level: "error", text: "Missing FROM clause." });
  }
  if (unterminatedSingleQuote(trimmed)) {
    messages.push({ level: "error", text: "Unterminated single-quoted string." });
  }
  if (!hasBalancedParens(trimmed)) {
    messages.push({ level: "error", text: "Unbalanced parentheses." });
  }
  if (!/;\s*$/.test(trimmed)) {
    messages.push({ level: "warning", text: "Query does not end with a semicolon." });
  }
  return messages;
}

function renderErrorsPane() {
  const query = getQueryText();
  const lintMessages = lintEnabled ? lintQuery(query) : [];
  const lines = [];

  if (lintEnabled) {
    if (lintMessages.length === 0) {
      lines.push("Lint: no issues detected.");
    } else {
      for (const message of lintMessages) {
        lines.push(`${message.level.toUpperCase()}: ${message.text}`);
      }
    }
  } else {
    lines.push("Lint is off.");
  }

  if (lastErrorText) {
    lines.push("");
    lines.push(`Runtime: ${lastErrorText}`);
  }

  const text = lines.join("\n").trim() || "No errors.";
  ui.errorsOutput.textContent = text;
  ui.errorsOutput.classList.toggle("empty", text === "No errors.");
  ui.errorsOutput.classList.toggle("error", !!lastErrorText || lintMessages.some((m) => m.level === "error"));
}

function renderJsonPane() {
  if (!lastResult || !Array.isArray(lastResult.columns) || !Array.isArray(lastResult.rows)) {
    ui.jsonOutput.textContent = "No result yet.";
    ui.jsonOutput.classList.add("empty");
    return;
  }
  ui.jsonOutput.textContent = buildJson(lastResult.columns, lastResult.rows);
  ui.jsonOutput.classList.remove("empty");
}

function renderResultsTable(result) {
  ui.resultsHead.innerHTML = "";
  ui.resultsBody.innerHTML = "";

  const headTr = document.createElement("tr");
  for (const col of result.columns) {
    const th = document.createElement("th");
    th.textContent = col.name;
    headTr.appendChild(th);
  }
  ui.resultsHead.appendChild(headTr);

  for (const row of result.rows) {
    const tr = document.createElement("tr");
    for (const value of row) {
      const td = document.createElement("td");
      td.textContent = valueToText(value);
      tr.appendChild(td);
    }
    ui.resultsBody.appendChild(tr);
  }
}

function setActiveTab(tabName) {
  activeOutputTab = tabName;
  for (const button of ui.tabButtons) {
    const active = button.dataset.tab === tabName;
    button.classList.toggle("active", active);
    button.setAttribute("aria-selected", active ? "true" : "false");
  }
  for (const pane of ui.tabPanes) {
    pane.classList.toggle("active", pane.dataset.pane === tabName);
  }
}

function updateCopyExportLabel() {
  if (activeOutputTab === "table") {
    ui.copyExportBtn.textContent = "Copy CSV";
  } else if (activeOutputTab === "json") {
    ui.copyExportBtn.textContent = "Copy JSON";
  } else {
    ui.copyExportBtn.textContent = "Copy Errors";
  }
}

function refreshDerivedViews() {
  renderLineNumbers();
  renderJsonPane();
  renderErrorsPane();
  updateCopyExportLabel();
}

async function getActiveTab() {
  const tabs = await chrome.tabs.query({ active: true, currentWindow: true });
  if (!tabs.length || typeof tabs[0].id !== "number") {
    throw new Error("No active tab available");
  }
  return tabs[0];
}

async function saveSnapshotToSession(html, scope) {
  if (!chrome.storage.session) return false;
  try {
    await chrome.storage.session.set({
      [STORAGE_KEY_SNAPSHOT]: html,
      [STORAGE_KEY_SNAPSHOT_SCOPE]: scope
    });
    return true;
  } catch (err) {
    console.warn("Failed to cache snapshot in session storage:", err);
    return false;
  }
}

async function restoreSnapshotFromSession() {
  if (!chrome.storage.session) return { html: "", scope: "" };
  const data = await chrome.storage.session.get([STORAGE_KEY_SNAPSHOT, STORAGE_KEY_SNAPSHOT_SCOPE]);
  const html = typeof data[STORAGE_KEY_SNAPSHOT] === "string" ? data[STORAGE_KEY_SNAPSHOT] : "";
  const scope = typeof data[STORAGE_KEY_SNAPSHOT_SCOPE] === "string" ? data[STORAGE_KEY_SNAPSHOT_SCOPE] : "";
  return { html, scope };
}

async function requestSnapshot(tabId, scope) {
  return chrome.runtime.sendMessage({
    type: "captureSnapshot",
    tabId,
    scope
  });
}

async function captureSnapshot(force = false) {
  if (!force && snapshotHtml && snapshotScope === PRIMARY_CAPTURE_SCOPE) {
    return snapshotHtml;
  }

  const tab = await getActiveTab();
  status(`Capturing ${PRIMARY_CAPTURE_SCOPE} snapshot...`);

  let response;
  try {
    response = await requestSnapshot(tab.id, PRIMARY_CAPTURE_SCOPE);
  } catch (err) {
    if (FALLBACK_CAPTURE_SCOPE === PRIMARY_CAPTURE_SCOPE) {
      throw err;
    }
    status(`Full capture failed, retrying ${FALLBACK_CAPTURE_SCOPE}...`);
    response = await requestSnapshot(tab.id, FALLBACK_CAPTURE_SCOPE);
  }

  if (!response || !response.ok || typeof response.html !== "string") {
    throw new Error(response && response.error ? response.error : "Capture failed");
  }

  snapshotHtml = response.html;
  snapshotScope = response.scope || PRIMARY_CAPTURE_SCOPE;
  const cached = await saveSnapshotToSession(snapshotHtml, snapshotScope);
  const captureSource = response.source || "unknown";
  status(
    `Captured ${response.size_bytes} bytes (${snapshotScope}/${captureSource})${cached ? "" : " | cache skipped"}.`
  );
  return snapshotHtml;
}

function getTokenOrExplain() {
  const token = ui.tokenInput.value.trim();
  if (token) {
    ui.tokenHelp.textContent = "";
    return token;
  }
  ui.tokenHelp.textContent = "Start MarkQL agent and copy token from terminal output.";
  throw new Error("Missing token");
}

async function runQuery() {
  const token = getTokenOrExplain();
  const query = getQueryText().trim();
  const lintMessages = lintEnabled ? lintQuery(query) : [];
  if (lintMessages.some((message) => message.level === "error")) {
    lastErrorText = "Query has lint errors. Fix them or turn lint off.";
    renderErrorsPane();
    setActiveTab("errors");
    updateCopyExportLabel();
    throw new Error(lastErrorText);
  }
  if (!query) {
    throw new Error("Query is required");
  }

  await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: query });

  if (!snapshotHtml || snapshotScope !== PRIMARY_CAPTURE_SCOPE) {
    snapshotHtml = await captureSnapshot(false);
  }

  const maxRows = normalizeNumber(ui.maxRowsInput.value, 2000, 1, 10000);
  const timeoutMs = normalizeNumber(ui.timeoutInput.value, 5000, 100, 120000);

  status("Running query...");

  const response = await fetch(AGENT_URL, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-XSQL-Token": token
    },
    body: JSON.stringify({
      html: snapshotHtml,
      query,
      options: {
        max_rows: maxRows,
        timeout_ms: timeoutMs
      }
    })
  });

  const payload = await response.json();
  if (!response.ok) {
    lastErrorText = payload && payload.error ? payload.error.message : `HTTP ${response.status}`;
    renderErrorsPane();
    setActiveTab("errors");
    updateCopyExportLabel();
    throw new Error(lastErrorText);
  }

  lastErrorText = "";
  lastResult = payload;
  renderResultsTable(payload);
  renderJsonPane();
  renderErrorsPane();
  setActiveTab("table");
  updateCopyExportLabel();

  const err = payload.error ? ` error=${payload.error.message}` : "";
  const trunc = payload.truncated ? " (truncated)" : "";
  status(`Snapshot ${snapshotHtml.length} bytes | elapsed ${payload.elapsed_ms} ms | rows ${payload.rows.length}${trunc}${err}`);
}

async function copyQuery() {
  const query = getQueryText();
  if (!query.trim()) {
    throw new Error("No query to copy");
  }
  await navigator.clipboard.writeText(query);
  status("Copied query.");
}

async function copyActiveExport() {
  if (activeOutputTab === "table") {
    if (!lastResult || !Array.isArray(lastResult.columns) || !Array.isArray(lastResult.rows)) {
      throw new Error("No table result to export");
    }
    const csv = buildCsv(lastResult.columns, lastResult.rows);
    await navigator.clipboard.writeText(csv);
    status(`Copied CSV (${lastResult.rows.length} rows).`);
    return;
  }
  if (activeOutputTab === "json") {
    if (!lastResult || !Array.isArray(lastResult.columns) || !Array.isArray(lastResult.rows)) {
      throw new Error("No JSON result to export");
    }
    await navigator.clipboard.writeText(buildJson(lastResult.columns, lastResult.rows));
    status(`Copied JSON (${lastResult.rows.length} rows).`);
    return;
  }
  const errorsText = ui.errorsOutput.textContent || "";
  if (!errorsText.trim()) {
    throw new Error("No errors to copy");
  }
  await navigator.clipboard.writeText(errorsText);
  status("Copied errors.");
}

async function applySelectedExample() {
  const value = ui.examplesSelect.value;
  if (!value) return;
  setQueryText(value);
  renderQueryHighlight();
  renderErrorsPane();
  await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: value });
  status("Loaded example query.");
  ui.examplesSelect.selectedIndex = 0;
  focusQueryInput(true);
}

async function formatCurrentQuery() {
  const formatted = formatQueryText(getQueryText());
  if (!formatted) {
    throw new Error("No query to format");
  }
  setQueryText(formatted);
  renderQueryHighlight();
  renderErrorsPane();
  await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: formatted });
  status("Formatted query.");
  focusQueryInput(true);
}

async function toggleLint() {
  lintEnabled = !lintEnabled;
  ui.lintBtn.setAttribute("aria-pressed", lintEnabled ? "true" : "false");
  await chrome.storage.local.set({ [STORAGE_KEY_LINT]: lintEnabled });
  renderErrorsPane();
  status(lintEnabled ? "Lint enabled." : "Lint disabled.");
}

function setTokenEditorVisible(visible) {
  ui.tokenEditor.classList.toggle("hidden", !visible);
  if (visible) {
    ui.tokenInput.focus();
  }
}

function updateTokenUi() {
  const hasToken = !!ui.tokenInput.value.trim();
  const editorVisible = !ui.tokenEditor.classList.contains("hidden");

  ui.tokenCompact.classList.toggle("hidden", !hasToken || editorVisible);
  ui.cancelTokenBtn.classList.toggle("hidden", !hasToken);

  if (!hasToken) {
    ui.tokenHelp.textContent = "Start MarkQL agent and copy token from terminal output.";
  } else if (editorVisible) {
    ui.tokenHelp.textContent = "Paste a new token and save.";
  } else {
    ui.tokenHelp.textContent = "";
  }
}

async function saveToken() {
  const token = ui.tokenInput.value.trim();
  await chrome.storage.local.set({ [STORAGE_KEY_TOKEN]: token });
  if (!token) {
    setTokenEditorVisible(true);
    updateTokenUi();
    return;
  }
  setTokenEditorVisible(false);
  updateTokenUi();
  status("Token saved.");
}

async function restoreSettings() {
  const localData = await chrome.storage.local.get([
    STORAGE_KEY_TOKEN,
    STORAGE_KEY_QUERY,
    STORAGE_KEY_LINT,
    LEGACY_STORAGE_KEY_TOKEN,
    LEGACY_STORAGE_KEY_QUERY
  ]);
  const token = localData[STORAGE_KEY_TOKEN] || localData[LEGACY_STORAGE_KEY_TOKEN];
  const query = localData[STORAGE_KEY_QUERY] || localData[LEGACY_STORAGE_KEY_QUERY];

  if (typeof token === "string") {
    ui.tokenInput.value = token;
    if (localData[STORAGE_KEY_TOKEN] !== token) {
      await chrome.storage.local.set({ [STORAGE_KEY_TOKEN]: token });
    }
  }
  if (typeof query === "string") {
    setQueryText(query);
    if (localData[STORAGE_KEY_QUERY] !== query) {
      await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: query });
    }
  }
  if (typeof localData[STORAGE_KEY_LINT] === "boolean") {
    lintEnabled = localData[STORAGE_KEY_LINT];
  }
  ui.lintBtn.setAttribute("aria-pressed", lintEnabled ? "true" : "false");

  const restoredSnapshot = await restoreSnapshotFromSession();
  snapshotHtml = restoredSnapshot.html;
  snapshotScope = restoredSnapshot.scope;
  if (!snapshotHtml && chrome.storage.session) {
    const legacySnapshotData = await chrome.storage.session.get([LEGACY_STORAGE_KEY_SNAPSHOT]);
    const legacySnapshot = legacySnapshotData[LEGACY_STORAGE_KEY_SNAPSHOT];
    if (typeof legacySnapshot === "string" && legacySnapshot) {
      snapshotHtml = legacySnapshot;
      snapshotScope = "";
      await saveSnapshotToSession(snapshotHtml, snapshotScope);
    }
  }
  if (snapshotHtml) {
    const scopeLabel = snapshotScope || "unknown";
    status(`Restored cached snapshot (${snapshotHtml.length} bytes, scope=${scopeLabel}).`);
  } else {
    status("Ready.");
  }

  const hasToken = !!ui.tokenInput.value.trim();
  setTokenEditorVisible(!hasToken);
  updateTokenUi();
  renderQueryHighlight();
  refreshDerivedViews();
  setActiveTab("table");
}

async function guarded(action) {
  const controls = [
    ui.captureBtn,
    ui.runBtn,
    ui.examplesSelect,
    ui.formatBtn,
    ui.lintBtn,
    ui.copyQueryBtn,
    ui.copyExportBtn,
    ui.saveTokenBtn,
    ui.cancelTokenBtn,
    ui.editTokenBtn
  ];

  try {
    for (const control of controls) {
      control.disabled = true;
    }
    await action();
  } catch (err) {
    status(`Error: ${err && err.message ? err.message : String(err)}`);
  } finally {
    for (const control of controls) {
      control.disabled = false;
    }
  }
}

ui.saveTokenBtn.addEventListener("click", () => guarded(saveToken));
ui.editTokenBtn.addEventListener("click", () => {
  setTokenEditorVisible(true);
  updateTokenUi();
});
ui.cancelTokenBtn.addEventListener("click", () => {
  if (ui.tokenInput.value.trim()) {
    setTokenEditorVisible(false);
  }
  updateTokenUi();
});
ui.captureBtn.addEventListener("click", () => guarded(() => captureSnapshot(true)));
ui.runBtn.addEventListener("click", () => guarded(runQuery));
ui.examplesSelect.addEventListener("change", () => guarded(applySelectedExample));
ui.formatBtn.addEventListener("click", () => guarded(formatCurrentQuery));
ui.lintBtn.addEventListener("click", () => guarded(toggleLint));
ui.copyQueryBtn.addEventListener("click", () => guarded(copyQuery));
ui.copyExportBtn.addEventListener("click", () => guarded(copyActiveExport));

for (const button of ui.tabButtons) {
  button.addEventListener("click", () => {
    setActiveTab(button.dataset.tab);
    updateCopyExportLabel();
  });
}

ui.sqlEditor.addEventListener("mousedown", (event) => {
  if (event.button !== 0) return;
  if (event.target === ui.queryInput || ui.queryInput.contains(event.target)) return;
  event.preventDefault();
  focusQueryInput(true);
});

ui.queryInput.addEventListener("scroll", syncEditorScroll);
ui.queryInput.addEventListener("input", () => {
  renderQueryHighlight(true);
  renderErrorsPane();
});
ui.queryInput.addEventListener("compositionstart", () => {
  isComposingQuery = true;
});
ui.queryInput.addEventListener("compositionend", () => {
  isComposingQuery = false;
  renderQueryHighlight(true);
  renderErrorsPane();
});
ui.queryInput.addEventListener("keydown", (event) => {
  if (event.isComposing || isComposingQuery) return;

  if ((event.metaKey || event.ctrlKey) && event.key === "Enter") {
    event.preventDefault();
    guarded(runQuery);
    return;
  }
  if (event.key === "Enter") {
    event.preventDefault();
    insertAtCaret("\n");
    renderQueryHighlight(true);
    renderErrorsPane();
    return;
  }
  if (event.key === "Tab") {
    event.preventDefault();
    insertAtCaret("  ");
    renderQueryHighlight(true);
    renderErrorsPane();
  }
});
ui.queryInput.addEventListener("paste", (event) => {
  event.preventDefault();
  const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
  if (text) {
    insertAtCaret(text);
    renderQueryHighlight(true);
    renderErrorsPane();
  }
});

renderQueryHighlight();
refreshDerivedViews();
setActiveTab("table");
restoreSettings().catch((err) => {
  status(`Error: ${err && err.message ? err.message : String(err)}`);
});
