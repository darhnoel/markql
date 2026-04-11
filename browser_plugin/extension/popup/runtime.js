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
  STORAGE_KEY_SNAPSHOT_DOCS,
  STORAGE_KEY_SNAPSHOT_ID,
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

  function normalizeSnapshotDocuments(rawDocuments) {
    if (!Array.isArray(rawDocuments)) return [];
    return rawDocuments
      .filter((doc) => doc && typeof doc.id === "string" && typeof doc.html === "string")
      .map((doc) => ({
        id: doc.id,
        frameId: typeof doc.frameId === "number" ? doc.frameId : -1,
        parentFrameId: typeof doc.parentFrameId === "number" ? doc.parentFrameId : null,
        url: typeof doc.url === "string" ? doc.url : "",
        title: typeof doc.title === "string" ? doc.title : "",
        frameName: typeof doc.frameName === "string" ? doc.frameName : "",
        frameElementId: typeof doc.frameElementId === "string" ? doc.frameElementId : "",
        isTop: !!doc.isTop,
        source: typeof doc.source === "string" ? doc.source : "unknown",
        html: doc.html,
        sizeBytes:
          typeof doc.sizeBytes === "number" && Number.isFinite(doc.sizeBytes)
            ? doc.sizeBytes
            : doc.html.length
      }));
  }

  function summarizeSnapshotUrl(url) {
    if (!url) return "";
    try {
      const parsed = new URL(url);
      if (parsed.pathname && parsed.pathname !== "/") {
        return parsed.pathname + parsed.search;
      }
      return parsed.host;
    } catch (_err) {
      return url;
    }
  }

  function buildSnapshotLabel(doc) {
    if (!doc) return "Document";
    let base = doc.isTop ? "Top document" : "Frame";
    if (!doc.isTop && doc.frameName) {
      base += ` ${doc.frameName}`;
    } else if (!doc.isTop && doc.frameElementId) {
      base += ` #${doc.frameElementId}`;
    } else if (doc.title) {
      base += `: ${doc.title}`;
    }
    const suffix = summarizeSnapshotUrl(doc.url);
    return suffix ? `${base} (${suffix})` : base;
  }

  function choosePreferredSnapshotId(documents) {
    if (!documents.length) return "";
    const frameDocuments = documents.filter((doc) => !doc.isTop);
    if (frameDocuments.length) {
      frameDocuments.sort((left, right) => right.html.length - left.html.length);
      return frameDocuments[0].id;
    }
    return documents[0].id;
  }

  function updateSnapshotPicker() {
    const documents = Array.isArray(state.snapshotDocuments) ? state.snapshotDocuments : [];
    ui.snapshotSelect.innerHTML = "";
    if (documents.length <= 1) {
      ui.snapshotPickerWrap.classList.add("hidden");
      return;
    }

    for (const doc of documents) {
      const option = document.createElement("option");
      option.value = doc.id;
      option.textContent = buildSnapshotLabel(doc);
      ui.snapshotSelect.appendChild(option);
    }
    ui.snapshotSelect.value = state.snapshotId;
    ui.snapshotPickerWrap.classList.remove("hidden");
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

  async function saveSnapshotToSession(
    html,
    scope,
    documents = state.snapshotDocuments,
    snapshotId = state.snapshotId
  ) {
    const normalizedDocuments = normalizeSnapshotDocuments(documents);
    if (!chrome.storage.session) return false;
    try {
      await chrome.storage.session.set({
        [STORAGE_KEY_SNAPSHOT]: html,
        [STORAGE_KEY_SNAPSHOT_SCOPE]: scope,
        [STORAGE_KEY_SNAPSHOT_DOCS]: JSON.stringify(normalizedDocuments),
        [STORAGE_KEY_SNAPSHOT_ID]: snapshotId
      });
      return true;
    } catch (err) {
      console.warn("Failed to cache snapshot in session storage:", err);
      return false;
    }
  }

  async function restoreSnapshotFromSession() {
    if (!chrome.storage.session) return { html: "", scope: "", documents: [], snapshotId: "" };
    const data = await chrome.storage.session.get([
      STORAGE_KEY_SNAPSHOT,
      STORAGE_KEY_SNAPSHOT_SCOPE,
      STORAGE_KEY_SNAPSHOT_DOCS,
      STORAGE_KEY_SNAPSHOT_ID
    ]);
    const html = typeof data[STORAGE_KEY_SNAPSHOT] === "string" ? data[STORAGE_KEY_SNAPSHOT] : "";
    const scope = typeof data[STORAGE_KEY_SNAPSHOT_SCOPE] === "string" ? data[STORAGE_KEY_SNAPSHOT_SCOPE] : "";
    const snapshotId =
      typeof data[STORAGE_KEY_SNAPSHOT_ID] === "string" ? data[STORAGE_KEY_SNAPSHOT_ID] : "";
    let documents = [];
    if (typeof data[STORAGE_KEY_SNAPSHOT_DOCS] === "string" && data[STORAGE_KEY_SNAPSHOT_DOCS]) {
      try {
        documents = normalizeSnapshotDocuments(JSON.parse(data[STORAGE_KEY_SNAPSHOT_DOCS]));
      } catch (_err) {
        documents = [];
      }
    }
    return { html, scope, documents, snapshotId };
  }

  async function requestSnapshot(tabId, scope) {
    return chrome.runtime.sendMessage({
      type: "captureSnapshot",
      tabId,
      scope
    });
  }

  function clearResultsForSnapshotChange() {
    state.lastResult = null;
    panes.clearResultsTable();
    panes.renderJsonPane();
    panes.renderErrorsPane();
    panes.setActiveTab("table");
    panes.updateCopyExportLabel();
  }

  async function selectSnapshot(snapshotId, options = {}) {
    const { persist = true, announce = true, clearResults = true } = options;
    if (!Array.isArray(state.snapshotDocuments) || !state.snapshotDocuments.length) {
      throw new Error("No captured documents available");
    }

    const selected =
      state.snapshotDocuments.find((doc) => doc.id === snapshotId) || state.snapshotDocuments[0];
    state.snapshotId = selected.id;
    state.snapshotHtml = selected.html;
    ui.snapshotSelect.value = selected.id;

    if (clearResults) {
      clearRunError();
      clearResultsForSnapshotChange();
    }
    if (persist) {
      await saveSnapshotToSession(
        state.snapshotHtml,
        state.snapshotScope,
        state.snapshotDocuments,
        state.snapshotId
      );
    }
    if (announce) {
      panes.status(`Selected ${buildSnapshotLabel(selected)} (${selected.html.length} bytes).`);
    }
    return selected.html;
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

    const documents = normalizeSnapshotDocuments(response && response.documents);
    if (!response || !response.ok || (!documents.length && typeof response.html !== "string")) {
      throw new Error(response && response.error ? response.error : "Capture failed");
    }

    state.snapshotDocuments = documents.length
      ? documents
      : normalizeSnapshotDocuments([
          {
            id: "frame-0",
            frameId: 0,
            parentFrameId: null,
            url: "",
            title: "",
            frameName: "",
            frameElementId: "",
            isTop: true,
            source: response.source || "unknown",
            html: response.html,
            sizeBytes: response.html.length
          }
        ]);
    state.snapshotScope = response.scope || PRIMARY_CAPTURE_SCOPE;
    updateSnapshotPicker();
    const preferredSnapshotId = choosePreferredSnapshotId(state.snapshotDocuments);
    await selectSnapshot(preferredSnapshotId, { persist: false, announce: false, clearResults: false });
    const cached = await saveSnapshotToSession(
      state.snapshotHtml,
      state.snapshotScope,
      state.snapshotDocuments,
      state.snapshotId
    );
    const captureSource = response.source || "unknown";
    const inaccessibleCount = Array.isArray(response.inaccessible_frames)
      ? response.inaccessible_frames.length
      : 0;
    const selectedDocument = state.snapshotDocuments.find((doc) => doc.id === state.snapshotId);
    panes.status(
      `Captured ${state.snapshotDocuments.length} document${state.snapshotDocuments.length === 1 ? "" : "s"} ` +
        `(${state.snapshotScope}/${captureSource}, selected=${buildSnapshotLabel(selectedDocument)})` +
        `${inaccessibleCount ? ` | ${inaccessibleCount} frame${inaccessibleCount === 1 ? "" : "s"} unavailable` : ""}` +
        `${cached ? "" : " | cache skipped"}.`
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
        "X-MarkQL-Token": token
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
    state.snapshotDocuments = restoredSnapshot.documents;
    state.snapshotId = restoredSnapshot.snapshotId;
    if (!state.snapshotHtml && chrome.storage.session) {
      const legacySnapshotData = await chrome.storage.session.get([LEGACY_STORAGE_KEY_SNAPSHOT]);
      const legacySnapshot = legacySnapshotData[LEGACY_STORAGE_KEY_SNAPSHOT];
      if (typeof legacySnapshot === "string" && legacySnapshot) {
        state.snapshotHtml = legacySnapshot;
        state.snapshotScope = "";
        state.snapshotDocuments = normalizeSnapshotDocuments([
          {
            id: "frame-0",
            frameId: 0,
            parentFrameId: null,
            url: "",
            title: "",
            frameName: "",
            frameElementId: "",
            isTop: true,
            source: "legacy",
            html: state.snapshotHtml,
            sizeBytes: state.snapshotHtml.length
          }
        ]);
        state.snapshotId = "frame-0";
        await saveSnapshotToSession(
          state.snapshotHtml,
          state.snapshotScope,
          state.snapshotDocuments,
          state.snapshotId
        );
      }
    }
    if (state.snapshotHtml) {
      if (!state.snapshotDocuments.length) {
        state.snapshotDocuments = normalizeSnapshotDocuments([
          {
            id: "frame-0",
            frameId: 0,
            parentFrameId: null,
            url: "",
            title: "",
            frameName: "",
            frameElementId: "",
            isTop: true,
            source: "restored",
            html: state.snapshotHtml,
            sizeBytes: state.snapshotHtml.length
          }
        ]);
      }
      updateSnapshotPicker();
      const restoredId =
        state.snapshotId && state.snapshotDocuments.some((doc) => doc.id === state.snapshotId)
          ? state.snapshotId
          : choosePreferredSnapshotId(state.snapshotDocuments);
      await selectSnapshot(restoredId, { persist: false, announce: false, clearResults: false });
      const scopeLabel = state.snapshotScope || "unknown";
      const selectedDocument = state.snapshotDocuments.find((doc) => doc.id === state.snapshotId);
      panes.status(
        `Restored cached snapshot (${state.snapshotHtml.length} bytes, scope=${scopeLabel}, selected=${buildSnapshotLabel(selectedDocument)}).`
      );
    } else {
      state.snapshotDocuments = [];
      state.snapshotId = "";
      updateSnapshotPicker();
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
      ui.snapshotSelect,
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
    selectSnapshot,
    setTokenEditorVisible,
    toggleLint,
    toggleQueryVisibility,
    updateTokenUi
  };
}
