const AGENT_URL = "http://127.0.0.1:7337/v1/query";
const STORAGE_KEY_TOKEN = "xsqlAgentToken";
const STORAGE_KEY_QUERY = "xsqlLastQuery";
const STORAGE_KEY_SNAPSHOT = "xsqlSnapshotHtml";

let snapshotHtml = "";
let lastResult = null;

const ui = {
  tokenInput: document.getElementById("tokenInput"),
  saveTokenBtn: document.getElementById("saveTokenBtn"),
  tokenHelp: document.getElementById("tokenHelp"),
  queryInput: document.getElementById("queryInput"),
  maxRowsInput: document.getElementById("maxRowsInput"),
  timeoutInput: document.getElementById("timeoutInput"),
  captureBtn: document.getElementById("captureBtn"),
  runBtn: document.getElementById("runBtn"),
  recaptureBtn: document.getElementById("recaptureBtn"),
  copyCsvBtn: document.getElementById("copyCsvBtn"),
  statusLine: document.getElementById("statusLine"),
  resultsHead: document.querySelector("#resultsTable thead"),
  resultsBody: document.querySelector("#resultsTable tbody")
};

function selectedScope() {
  const selected = document.querySelector("input[name='scope']:checked");
  return selected && selected.value === "full" ? "full" : "main";
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

async function saveSnapshotToSession(html) {
  if (!chrome.storage.session) return;
  await chrome.storage.session.set({ [STORAGE_KEY_SNAPSHOT]: html });
}

async function restoreSnapshotFromSession() {
  if (!chrome.storage.session) return "";
  const data = await chrome.storage.session.get([STORAGE_KEY_SNAPSHOT]);
  return typeof data[STORAGE_KEY_SNAPSHOT] === "string" ? data[STORAGE_KEY_SNAPSHOT] : "";
}

async function captureSnapshot(force = false) {
  if (!force && snapshotHtml) {
    return snapshotHtml;
  }

  const tab = await getActiveTab();
  status("Capturing snapshot...");

  const response = await chrome.runtime.sendMessage({
    type: "captureSnapshot",
    tabId: tab.id,
    scope: selectedScope()
  });

  if (!response || !response.ok || typeof response.html !== "string") {
    throw new Error(response && response.error ? response.error : "Capture failed");
  }

  snapshotHtml = response.html;
  await saveSnapshotToSession(snapshotHtml);
  status(`Captured ${response.size_bytes} bytes (${response.scope}, ${response.source}).`);
  return snapshotHtml;
}

function getTokenOrExplain() {
  const token = ui.tokenInput.value.trim();
  if (token) {
    ui.tokenHelp.textContent = "";
    return token;
  }
  ui.tokenHelp.textContent = "Start xsql-agent and copy token from terminal output.";
  throw new Error("Missing token");
}

async function runQuery() {
  const token = getTokenOrExplain();
  const query = ui.queryInput.value.trim();
  if (!query) {
    throw new Error("Query is required");
  }

  await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: query });

  if (!snapshotHtml) {
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

async function saveToken() {
  const token = ui.tokenInput.value.trim();
  await chrome.storage.local.set({ [STORAGE_KEY_TOKEN]: token });
  if (!token) {
    ui.tokenHelp.textContent = "Start xsql-agent and copy token from terminal output.";
    return;
  }
  ui.tokenHelp.textContent = "Token saved.";
  status("Token saved.");
}

async function restoreSettings() {
  const localData = await chrome.storage.local.get([STORAGE_KEY_TOKEN, STORAGE_KEY_QUERY]);
  const token = localData[STORAGE_KEY_TOKEN];
  const query = localData[STORAGE_KEY_QUERY];

  if (typeof token === "string") {
    ui.tokenInput.value = token;
  }
  if (typeof query === "string") {
    ui.queryInput.value = query;
  }

  snapshotHtml = await restoreSnapshotFromSession();
  if (snapshotHtml) {
    status(`Restored cached snapshot (${snapshotHtml.length} bytes).`);
  } else {
    status("Ready.");
  }

  if (!ui.tokenInput.value.trim()) {
    ui.tokenHelp.textContent = "Start xsql-agent and copy token from terminal output.";
  }
}

async function guarded(action) {
  try {
    ui.captureBtn.disabled = true;
    ui.runBtn.disabled = true;
    ui.recaptureBtn.disabled = true;
    ui.copyCsvBtn.disabled = true;
    await action();
  } catch (err) {
    status(`Error: ${err && err.message ? err.message : String(err)}`);
  } finally {
    ui.captureBtn.disabled = false;
    ui.runBtn.disabled = false;
    ui.recaptureBtn.disabled = false;
    ui.copyCsvBtn.disabled = false;
  }
}

ui.saveTokenBtn.addEventListener("click", () => guarded(saveToken));
ui.captureBtn.addEventListener("click", () => guarded(() => captureSnapshot(false)));
ui.recaptureBtn.addEventListener("click", () => guarded(() => captureSnapshot(true)));
ui.runBtn.addEventListener("click", () => guarded(runQuery));
ui.copyCsvBtn.addEventListener("click", () => guarded(copyCsv));

restoreSettings().catch((err) => {
  status(`Error: ${err && err.message ? err.message : String(err)}`);
});
