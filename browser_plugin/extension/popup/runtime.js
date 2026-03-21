import {
  AGENT_URL,
  FALLBACK_CAPTURE_SCOPE,
  LEGACY_STORAGE_KEY_QUERY,
  LEGACY_STORAGE_KEY_SNAPSHOT,
  LEGACY_STORAGE_KEY_TOKEN,
  PRIMARY_CAPTURE_SCOPE,
  STORAGE_KEY_LINT,
  STORAGE_KEY_QUERY,
  STORAGE_KEY_QUERY_COLLAPSED,
  STORAGE_KEY_SNAPSHOT,
  STORAGE_KEY_SNAPSHOT_SCOPE,
  STORAGE_KEY_TOKEN
} from "./config.js";

export function createPopupRuntime({ ui, state, editor, panes }) {
  function pickPrimaryDiagnostic(diagnostics) {
    if (!Array.isArray(diagnostics) || diagnostics.length === 0) return null;
    return diagnostics.find((diagnostic) => diagnostic && diagnostic.severity === "ERROR") || diagnostics[0];
  }

  function normalizeNumber(value, fallback, min, max) {
    const parsed = Number(value);
    if (!Number.isFinite(parsed)) return fallback;
    return Math.min(max, Math.max(min, Math.floor(parsed)));
  }

  function buildErrorHint(message) {
    if (!message) return "";
    if (message.includes("Expected axis name")) {
      return "Use EXISTS(self|parent|child|ancestor|descendant WHERE ...) when adding a structural predicate.";
    }
    if (message.includes("Unterminated single-quoted string")) {
      return "Close the quoted string or escape inner single quotes by doubling them.";
    }
    if (message.includes("Unbalanced parentheses")) {
      return "Match every opening parenthesis with a closing parenthesis before running the query.";
    }
    if (message.includes("Query timed out")) {
      return "Tighten the row filter, lower max rows, or increase the timeout if the query is expected to scan a large snapshot.";
    }
    return "";
  }

  function normalizeRunError(error) {
    const diagnostic = pickPrimaryDiagnostic(error && error.diagnostics);
    if (diagnostic) {
      return {
        summary: diagnostic.message || "Query error",
        detail: diagnostic.why || diagnostic.snippet || error.message || "Unknown error",
        hint: diagnostic.help || buildErrorHint(error && error.message ? error.message : "")
      };
    }

    const message = error && error.message ? error.message : "Unknown error";

    if (message.startsWith("Query parse error: ")) {
      return {
        summary: "Parse error",
        detail: message.slice("Query parse error: ".length).trim() || message,
        hint: buildErrorHint(message)
      };
    }
    if (error && error.code === "TIMEOUT") {
      return { summary: "Timed out", detail: message, hint: buildErrorHint(message) };
    }
    if (error && error.title === "Request Error") {
      return { summary: "Request error", detail: message, hint: buildErrorHint(message) };
    }
    if (error && error.code === "LINT") {
      return { summary: "Lint error", detail: message, hint: buildErrorHint(message) };
    }

    return { summary: "Query error", detail: message, hint: buildErrorHint(message) };
  }

  function describeRunError(error) {
    if (!error) return "";
    return `${error.summary}. See Errors.`;
  }

  function setRunError(error) {
    const normalized = normalizeRunError(error);
    state.lastRunError = {
      title: error.title || "Query Error",
      code: error.code || "",
      message: error.message || "Unknown error",
      diagnostics: Array.isArray(error.diagnostics) ? error.diagnostics : [],
      summary: normalized.summary,
      detail: normalized.detail,
      hint: error.hint || normalized.hint || buildErrorHint(error.message || "")
    };
    state.lastResult = null;
    panes.clearResultsTable();
    panes.renderJsonPane();
    panes.renderErrorsPane();
    panes.setActiveTab("errors");
    panes.updateCopyExportLabel();
    panes.status(describeRunError(state.lastRunError), "error");
  }

  function clearRunError() {
    state.lastRunError = null;
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
    if (!force && state.snapshotHtml && state.snapshotScope === PRIMARY_CAPTURE_SCOPE) {
      return state.snapshotHtml;
    }

    const tab = await getActiveTab();
    panes.status(`Capturing ${PRIMARY_CAPTURE_SCOPE} snapshot...`);

    let response;
    try {
      response = await requestSnapshot(tab.id, PRIMARY_CAPTURE_SCOPE);
    } catch (err) {
      if (FALLBACK_CAPTURE_SCOPE === PRIMARY_CAPTURE_SCOPE) {
        throw err;
      }
      panes.status(`Full capture failed, retrying ${FALLBACK_CAPTURE_SCOPE}...`);
      response = await requestSnapshot(tab.id, FALLBACK_CAPTURE_SCOPE);
    }

    if (!response || !response.ok || typeof response.html !== "string") {
      throw new Error(response && response.error ? response.error : "Capture failed");
    }

    state.snapshotHtml = response.html;
    state.snapshotScope = response.scope || PRIMARY_CAPTURE_SCOPE;
    const cached = await saveSnapshotToSession(state.snapshotHtml, state.snapshotScope);
    const captureSource = response.source || "unknown";
    panes.status(
      `Captured ${response.size_bytes} bytes (${state.snapshotScope}/${captureSource})${cached ? "" : " | cache skipped"}.`
    );
    return state.snapshotHtml;
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
    const query = editor.getQueryText().trim();
    const lintMessages = state.lintEnabled ? editor.lintQuery(query) : [];
    if (lintMessages.some((message) => message.level === "error")) {
      setRunError({
        title: "Lint Error",
        code: "LINT",
        message: "Query has lint errors. Fix them or turn lint off."
      });
      throw new Error(describeRunError(state.lastRunError));
    }
    if (!query) {
      throw new Error("Query is required");
    }

    await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: query });

    if (!state.snapshotHtml || state.snapshotScope !== PRIMARY_CAPTURE_SCOPE) {
      state.snapshotHtml = await captureSnapshot(false);
    }

    const maxRows = normalizeNumber(ui.maxRowsInput.value, 2000, 1, 10000);
    const timeoutMs = normalizeNumber(ui.timeoutInput.value, 5000, 100, 120000);

    panes.status("Running query...");

    const response = await fetch(AGENT_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-XSQL-Token": token
      },
      body: JSON.stringify({
        html: state.snapshotHtml,
        query,
        options: {
          max_rows: maxRows,
          timeout_ms: timeoutMs
        }
      })
    });

    const payload = await response.json();
    if (!response.ok || (payload && payload.error)) {
      const errorPayload = payload && payload.error ? payload.error : null;
      setRunError({
        title: response.ok ? "Query Error" : "Request Error",
        code: errorPayload && errorPayload.code ? errorPayload.code : "",
        message: errorPayload && errorPayload.message ? errorPayload.message : `HTTP ${response.status}`,
        diagnostics: errorPayload && Array.isArray(errorPayload.diagnostics) ? errorPayload.diagnostics : []
      });
      throw new Error(describeRunError(state.lastRunError));
    }

    clearRunError();
    state.lastResult = payload;
    panes.renderResultsTable(payload);
    panes.renderJsonPane();
    panes.renderErrorsPane();
    panes.setActiveTab("table");
    panes.updateCopyExportLabel();

    const trunc = payload.truncated ? " (truncated)" : "";
    panes.status(
      `Snapshot ${state.snapshotHtml.length} bytes | elapsed ${payload.elapsed_ms} ms | rows ${payload.rows.length}${trunc}`
    );
  }

  async function copyQuery() {
    const query = editor.getQueryText();
    if (!query.trim()) {
      throw new Error("No query to copy");
    }
    await navigator.clipboard.writeText(query);
    panes.status("Copied query.");
  }

  async function copyActiveExport() {
    if (state.activeOutputTab === "table") {
      if (!state.lastResult || !Array.isArray(state.lastResult.columns) || !Array.isArray(state.lastResult.rows)) {
        throw new Error("No table result to export");
      }
      const csv = panes.buildCsv(state.lastResult.columns, state.lastResult.rows);
      await navigator.clipboard.writeText(csv);
      panes.status(`Copied CSV (${state.lastResult.rows.length} rows).`);
      return;
    }
    if (state.activeOutputTab === "json") {
      if (!state.lastResult || !Array.isArray(state.lastResult.columns) || !Array.isArray(state.lastResult.rows)) {
        throw new Error("No JSON result to export");
      }
      await navigator.clipboard.writeText(panes.buildJson(state.lastResult.columns, state.lastResult.rows));
      panes.status(`Copied JSON (${state.lastResult.rows.length} rows).`);
      return;
    }
    const errorsText = ui.errorsOutput.textContent || "";
    if (!errorsText.trim()) {
      throw new Error("No errors to copy");
    }
    await navigator.clipboard.writeText(errorsText);
    panes.status("Copied errors.");
  }

  async function applySelectedExample() {
    const value = ui.examplesSelect.value;
    if (!value) return;
    clearRunError();
    editor.replaceQueryText(value);
    panes.renderErrorsPane();
    await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: value });
    panes.status("Loaded example query.");
    ui.examplesSelect.selectedIndex = 0;
    editor.focusQueryInput(true);
  }

  async function formatCurrentQuery() {
    const formatted = editor.formatQueryText(editor.getQueryText());
    if (!formatted) {
      throw new Error("No query to format");
    }
    clearRunError();
    editor.replaceQueryText(formatted);
    panes.renderErrorsPane();
    await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: formatted });
    panes.status("Formatted query.");
    editor.focusQueryInput(true);
  }

  async function toggleLint() {
    state.lintEnabled = !state.lintEnabled;
    ui.lintBtn.setAttribute("aria-pressed", state.lintEnabled ? "true" : "false");
    await chrome.storage.local.set({ [STORAGE_KEY_LINT]: state.lintEnabled });
    panes.renderErrorsPane();
    panes.status(state.lintEnabled ? "Lint enabled." : "Lint disabled.");
  }

  async function toggleQueryVisibility() {
    state.queryCollapsed = !state.queryCollapsed;
    panes.applyQueryCollapsedUi();
    await chrome.storage.local.set({ [STORAGE_KEY_QUERY_COLLAPSED]: state.queryCollapsed });
    panes.status(state.queryCollapsed ? "Query hidden." : "Query shown.");
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
    ui.app.classList.toggle("has-token", hasToken);

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
    panes.status("Token saved.");
  }

  async function restoreSettings() {
    const localData = await chrome.storage.local.get([
      STORAGE_KEY_TOKEN,
      STORAGE_KEY_QUERY,
      STORAGE_KEY_QUERY_COLLAPSED,
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
      editor.replaceQueryText(query);
      if (localData[STORAGE_KEY_QUERY] !== query) {
        await chrome.storage.local.set({ [STORAGE_KEY_QUERY]: query });
      }
    }
    if (typeof localData[STORAGE_KEY_LINT] === "boolean") {
      state.lintEnabled = localData[STORAGE_KEY_LINT];
    }
    if (typeof localData[STORAGE_KEY_QUERY_COLLAPSED] === "boolean") {
      state.queryCollapsed = localData[STORAGE_KEY_QUERY_COLLAPSED];
    }
    ui.lintBtn.setAttribute("aria-pressed", state.lintEnabled ? "true" : "false");
    panes.applyQueryCollapsedUi();

    const restoredSnapshot = await restoreSnapshotFromSession();
    state.snapshotHtml = restoredSnapshot.html;
    state.snapshotScope = restoredSnapshot.scope;
    if (!state.snapshotHtml && chrome.storage.session) {
      const legacySnapshotData = await chrome.storage.session.get([LEGACY_STORAGE_KEY_SNAPSHOT]);
      const legacySnapshot = legacySnapshotData[LEGACY_STORAGE_KEY_SNAPSHOT];
      if (typeof legacySnapshot === "string" && legacySnapshot) {
        state.snapshotHtml = legacySnapshot;
        state.snapshotScope = "";
        await saveSnapshotToSession(state.snapshotHtml, state.snapshotScope);
      }
    }
    if (state.snapshotHtml) {
      const scopeLabel = state.snapshotScope || "unknown";
      panes.status(`Restored cached snapshot (${state.snapshotHtml.length} bytes, scope=${scopeLabel}).`);
    } else {
      panes.status("Ready.");
    }

    const hasToken = !!ui.tokenInput.value.trim();
    setTokenEditorVisible(!hasToken);
    updateTokenUi();
    editor.renderQueryHighlight();
    panes.refreshDerivedViews(editor.renderLineNumbers);
    panes.setActiveTab("table");
    editor.resetHistory();
  }

  async function guarded(action) {
    const controls = [
      ui.captureBtn,
      ui.runBtn,
      ui.toggleQueryBtn,
      ui.runResultsBtn,
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
      const message = err && err.message ? err.message : String(err);
      if (!state.lastRunError || message !== describeRunError(state.lastRunError)) {
        panes.status(`Error: ${message}`, "error");
      }
    } finally {
      for (const control of controls) {
        control.disabled = false;
      }
    }
  }

  return {
    applySelectedExample,
    captureSnapshot,
    clearRunError,
    copyActiveExport,
    copyQuery,
    formatCurrentQuery,
    guarded,
    restoreSettings,
    runQuery,
    saveToken,
    setTokenEditorVisible,
    toggleLint,
    toggleQueryVisibility,
    updateTokenUi
  };
}
