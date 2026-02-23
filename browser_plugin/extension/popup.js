const AGENT_URL = "http://127.0.0.1:7337/v1/query";
const STORAGE_KEY_TOKEN = "markqlAgentToken";
const STORAGE_KEY_QUERY = "markqlLastQuery";
const STORAGE_KEY_SNAPSHOT = "markqlSnapshotHtml";
const STORAGE_KEY_SNAPSHOT_SCOPE = "markqlSnapshotScope";
const LEGACY_STORAGE_KEY_TOKEN = "xsqlAgentToken";
const LEGACY_STORAGE_KEY_QUERY = "xsqlLastQuery";
const LEGACY_STORAGE_KEY_SNAPSHOT = "xsqlSnapshotHtml";
const PRIMARY_CAPTURE_SCOPE = "full";
const FALLBACK_CAPTURE_SCOPE = "main";

let snapshotHtml = "";
let snapshotScope = "";
let lastResult = null;
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
  queryInput: document.getElementById("queryInput"),
  maxRowsInput: document.getElementById("maxRowsInput"),
  timeoutInput: document.getElementById("timeoutInput"),
  captureBtn: document.getElementById("captureBtn"),
  runBtn: document.getElementById("runBtn"),
  copyCsvBtn: document.getElementById("copyCsvBtn"),
  copyJsonBtn: document.getElementById("copyJsonBtn"),
  statusLine: document.getElementById("statusLine"),
  resultsHead: document.querySelector("#resultsTable thead"),
  resultsBody: document.querySelector("#resultsTable tbody")
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
  if (!ui.queryInput) return "";
  return ui.queryInput.textContent || "";
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

function renderQueryHighlight(preserveCaret = false) {
  if (!ui.queryInput) return;
  const query = getQueryText();
  const caret = preserveCaret ? getCaretOffset(ui.queryInput) : 0;
  ui.queryInput.innerHTML = highlightSql(query);
  if (!query) {
    ui.queryInput.innerHTML = "";
  }
  if (preserveCaret) {
    setCaretOffset(ui.queryInput, caret);
  }
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

function renderResults(result) {
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
    // Large pages can exceed storage.session quota; keep in-memory snapshot usable.
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
  const captureScope = response.scope || PRIMARY_CAPTURE_SCOPE;
  snapshotScope = captureScope;
  const cached = await saveSnapshotToSession(snapshotHtml, snapshotScope);
  const captureSource = response.source || "unknown";
  status(
    `Captured ${response.size_bytes} bytes (${captureScope}/${captureSource})${cached ? "" : " | cache skipped"}.`
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
    const msg = payload && payload.error ? payload.error.message : `HTTP ${response.status}`;
    throw new Error(msg);
  }

  lastResult = payload;
  renderResults(payload);

  const err = payload.error ? ` error=${payload.error.message}` : "";
  const trunc = payload.truncated ? " (truncated)" : "";
  status(`Snapshot ${snapshotHtml.length} bytes | elapsed ${payload.elapsed_ms} ms | rows ${payload.rows.length}${trunc}${err}`);
}

async function copyCsv() {
  if (!lastResult || !Array.isArray(lastResult.columns) || !Array.isArray(lastResult.rows)) {
    throw new Error("No query result to export");
  }
  const csv = buildCsv(lastResult.columns, lastResult.rows);
  await navigator.clipboard.writeText(csv);
  status(`Copied CSV (${lastResult.rows.length} rows).`);
}

async function copyJson() {
  if (!lastResult || !Array.isArray(lastResult.columns) || !Array.isArray(lastResult.rows)) {
    throw new Error("No query result to export");
  }
  const json = buildJson(lastResult.columns, lastResult.rows);
  await navigator.clipboard.writeText(json);
  status(`Copied JSON (${lastResult.rows.length} rows).`);
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
}

async function guarded(action) {
  try {
    ui.captureBtn.disabled = true;
    ui.runBtn.disabled = true;
    ui.copyCsvBtn.disabled = true;
    ui.copyJsonBtn.disabled = true;
    ui.saveTokenBtn.disabled = true;
    ui.cancelTokenBtn.disabled = true;
    ui.editTokenBtn.disabled = true;
    await action();
  } catch (err) {
    status(`Error: ${err && err.message ? err.message : String(err)}`);
  } finally {
    ui.captureBtn.disabled = false;
    ui.runBtn.disabled = false;
    ui.copyCsvBtn.disabled = false;
    ui.copyJsonBtn.disabled = false;
    ui.saveTokenBtn.disabled = false;
    ui.cancelTokenBtn.disabled = false;
    ui.editTokenBtn.disabled = false;
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
ui.copyCsvBtn.addEventListener("click", () => guarded(copyCsv));
ui.copyJsonBtn.addEventListener("click", () => guarded(copyJson));
ui.queryInput.addEventListener("input", () => renderQueryHighlight(true));
ui.queryInput.addEventListener("keydown", (event) => {
  if (event.key === "Tab") {
    event.preventDefault();
    insertAtCaret("  ");
    renderQueryHighlight(true);
  }
});
ui.queryInput.addEventListener("paste", (event) => {
  event.preventDefault();
  const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
  if (text) {
    insertAtCaret(text);
    renderQueryHighlight(true);
  }
});
renderQueryHighlight();
restoreSettings().catch((err) => {
  status(`Error: ${err && err.message ? err.message : String(err)}`);
});
