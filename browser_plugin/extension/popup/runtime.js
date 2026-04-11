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

  function summarizeSnapshotUrl(url, options = {}) {
    const { compact = false } = options;
    if (!url) return "";
    try {
      const parsed = new URL(url);
      if (compact) {
        const frameName = parsed.searchParams.get("framename");
        const segments = parsed.pathname.split("/").filter(Boolean);
        const tail = segments.length ? segments[segments.length - 1] : "";
        if (frameName && tail) {
          return `${tail}?framename=${frameName}`;
        }
        if (tail) {
          return tail;
        }
        return parsed.host;
      }
      if (parsed.pathname && parsed.pathname !== "/") {
        return parsed.pathname + parsed.search;
      }
      return parsed.host;
    } catch (_err) {
      return url;
    }
  }

  function truncateOptionLabel(text, maxLength = 72) {
    const value = typeof text === "string" ? text.trim() : "";
    if (!value || value.length <= maxLength) return value;
    const shortened = value.slice(0, maxLength - 3);
    const lastSpace = shortened.lastIndexOf(" ");
    return lastSpace > 20 ? `${shortened.slice(0, lastSpace)}...` : `${shortened}...`;
  }

  function isBlankSnapshotDoc(doc) {
    if (!doc) return true;

    const url = (doc.url || "").trim().toLowerCase();
    const title = (doc.title || "").trim();
    const frameName = (doc.frameName || "").trim();
    const frameElementId = (doc.frameElementId || "").trim();
    const htmlSize = typeof doc.html === "string" ? doc.html.trim().length : 0;

    const blankUrl =
      !url ||
      url === "about:blank" ||
      url === "srcdoc" ||
      url.endsWith(":blank");

    const hasIdentity = !!(title || frameName || frameElementId);
    const tinyHtml = htmlSize < 80;

    return blankUrl && !hasIdentity && tinyHtml;
  }

  function buildSnapshotIdentity(doc) {
    if (!doc) return "Document";

    let base = doc.isTop ? "Top document" : "Frame";
    if (!doc.isTop && doc.frameName) {
      base += ` ${doc.frameName}`;
    } else if (!doc.isTop && doc.frameElementId) {
      base += ` #${doc.frameElementId}`;
    }

    if (doc.title) {
      return `${base}: ${doc.title}`;
    }

    const suffix = summarizeSnapshotUrl(doc.url, { compact: true });
    return suffix ? `${base} (${suffix})` : base;
  }

  function buildSnapshotLabel(doc, options = {}) {
    const { maxLength = 0 } = options;
    if (!doc) return "Document";

    let label = buildSnapshotIdentity(doc);
    if (isBlankSnapshotDoc(doc)) {
      label += " (blank)";
    }

    return maxLength > 0 ? truncateOptionLabel(label, maxLength) : label;
  }

  function buildSnapshotTooltip(doc) {
    if (!doc) return "Document";
    const label = buildSnapshotLabel(doc);
    const fullUrl = summarizeSnapshotUrl(doc.url);
    return fullUrl && !label.includes(fullUrl) ? `${label} [${fullUrl}]` : label;
  }

  function choosePreferredSnapshotId(documents) {
    if (!documents.length) return "";

    const usefulFrames = documents
      .filter((doc) => !doc.isTop && !isBlankSnapshotDoc(doc))
      .sort((left, right) => right.html.length - left.html.length);

    if (usefulFrames.length) {
      return usefulFrames[0].id;
    }

    const topDocument = documents.find((doc) => doc.isTop && !isBlankSnapshotDoc(doc));
    if (topDocument) {
      return topDocument.id;
    }

    const nonBlankAny = documents.find((doc) => !isBlankSnapshotDoc(doc));
    if (nonBlankAny) {
      return nonBlankAny.id;
    }

    return documents[0].id;
  }

  function updateSnapshotPicker() {
    const documents = Array.isArray(state.snapshotDocuments) ? [...state.snapshotDocuments] : [];
    ui.snapshotSelect.innerHTML = "";
    ui.snapshotDropdownMenu.innerHTML = "";

    if (documents.length <= 1) {
      ui.snapshotPickerWrap.classList.add("hidden");
      closeSnapshotDropdown();
      return;
    }

    documents.sort((a, b) => {
      const aBlank = isBlankSnapshotDoc(a) ? 1 : 0;
      const bBlank = isBlankSnapshotDoc(b) ? 1 : 0;
      if (aBlank !== bBlank) return aBlank - bBlank;
      return (b.html?.length || 0) - (a.html?.length || 0);
    });

    for (const doc of documents) {
      const fullLabel = buildSnapshotTooltip(doc);

      const option = document.createElement("option");
      option.value = doc.id;
      option.textContent = buildSnapshotLabel(doc, { maxLength: 56 });
      option.title = fullLabel;
      ui.snapshotSelect.appendChild(option);

      const item = document.createElement("button");
      item.type = "button";
      item.className = "snapshot-dropdown-item";
      item.role = "option";
      item.dataset.snapshotId = doc.id;
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", doc.id === state.snapshotId ? "true" : "false");
      item.title = fullLabel;
      item.classList.toggle("active", doc.id === state.snapshotId);

      const itemLabel = document.createElement("span");
      itemLabel.className = "snapshot-dropdown-item-label";
      itemLabel.textContent = buildSnapshotLabel(doc, { maxLength: 56 });
      item.appendChild(itemLabel);
      ui.snapshotDropdownMenu.appendChild(item);
    }

    ui.snapshotSelect.value = state.snapshotId;
    ui.snapshotDropdownLabel.textContent = buildSnapshotLabel(
      state.snapshotDocuments.find((doc) => doc.id === state.snapshotId) || documents[0],
      { maxLength: 56 }
    );
    ui.snapshotDropdownBtn.title = buildSnapshotTooltip(
      state.snapshotDocuments.find((doc) => doc.id === state.snapshotId) || documents[0]
    );
    ui.snapshotPickerWrap.classList.remove("hidden");
  }

  function updateExamplesPicker() {
    if (!ui.examplesSelect || !ui.examplesDropdownMenu || !ui.examplesDropdownLabel || !ui.examplesDropdownBtn) {
      return;
    }

    ui.examplesDropdownMenu.innerHTML = "";

    for (const option of Array.from(ui.examplesSelect.options || [])) {
      const item = document.createElement("button");
      item.type = "button";
      item.className = "examples-dropdown-item";
      item.dataset.exampleValue = option.value;
      item.setAttribute("role", "option");
      item.setAttribute("aria-selected", option.selected ? "true" : "false");
      item.classList.toggle("active", option.selected);
      item.title = option.textContent || "Examples";

      const itemLabel = document.createElement("span");
      itemLabel.className = "examples-dropdown-item-label";
      itemLabel.textContent = option.textContent || "Examples";
      item.appendChild(itemLabel);
      ui.examplesDropdownMenu.appendChild(item);
    }

    const selectedOption = ui.examplesSelect.selectedOptions && ui.examplesSelect.selectedOptions[0];
    const label = selectedOption && selectedOption.value ? (selectedOption.textContent || "Examples") : "Examples";
    ui.examplesDropdownLabel.textContent = label;
    ui.examplesDropdownBtn.title = label;
  }

  function setExamplesDropdownOpen(open) {
    if (!ui.examplesDropdownBtn || !ui.examplesDropdownMenu) return;
    ui.examplesDropdownMenu.classList.toggle("hidden", !open);
    ui.examplesDropdownBtn.classList.toggle("open", open);
    ui.examplesDropdownBtn.setAttribute("aria-expanded", open ? "true" : "false");
  }

  function closeExamplesDropdown() {
    setExamplesDropdownOpen(false);
  }

  function toggleExamplesDropdown() {
    const open = ui.examplesDropdownMenu.classList.contains("hidden");
    setExamplesDropdownOpen(open);
  }

  function refreshExamplesDropdownSelection() {
    if (!ui.examplesSelect || !ui.examplesDropdownMenu || !ui.examplesDropdownLabel || !ui.examplesDropdownBtn) {
      return;
    }
    const selectedOption = ui.examplesSelect.selectedOptions && ui.examplesSelect.selectedOptions[0];
    const label = selectedOption && selectedOption.value ? (selectedOption.textContent || "Examples") : "Examples";
    ui.examplesDropdownLabel.textContent = label;
    ui.examplesDropdownBtn.title = label;
    for (const item of ui.examplesDropdownMenu.querySelectorAll(".examples-dropdown-item")) {
      const active = item.dataset.exampleValue === ui.examplesSelect.value;
      item.setAttribute("aria-selected", active ? "true" : "false");
      item.classList.toggle("active", active);
    }
  }

  function setSnapshotDropdownOpen(open) {
    if (!ui.snapshotDropdownBtn || !ui.snapshotDropdownMenu) return;
    ui.snapshotDropdownMenu.classList.toggle("hidden", !open);
    ui.snapshotDropdownBtn.classList.toggle("open", open);
    ui.snapshotDropdownBtn.setAttribute("aria-expanded", open ? "true" : "false");
  }

  function closeSnapshotDropdown() {
    setSnapshotDropdownOpen(false);
  }

  function toggleSnapshotDropdown() {
    if (ui.snapshotPickerWrap.classList.contains("hidden")) return;
    const open = ui.snapshotDropdownMenu.classList.contains("hidden");
    setSnapshotDropdownOpen(open);
  }

  function refreshSnapshotDropdownSelection() {
    const selected = state.snapshotDocuments.find((doc) => doc.id === state.snapshotId);
    const labelDoc = selected || state.snapshotDocuments[0] || null;
    if (!labelDoc) return;
    ui.snapshotDropdownLabel.textContent = buildSnapshotLabel(labelDoc, { maxLength: 56 });
    ui.snapshotDropdownBtn.title = buildSnapshotTooltip(labelDoc);
    for (const item of ui.snapshotDropdownMenu.querySelectorAll(".snapshot-dropdown-item")) {
      const active = item.dataset.snapshotId === state.snapshotId;
      item.setAttribute("aria-selected", active ? "true" : "false");
      item.classList.toggle("active", active);
    }
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
    ui.snapshotSelect.title = buildSnapshotTooltip(selected);
    refreshSnapshotDropdownSelection();
    closeSnapshotDropdown();

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
      panes.status(`Selected ${buildSnapshotLabel(selected, { maxLength: 72 })} (${selected.html.length} bytes).`);
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

    const preferredSnapshotId = choosePreferredSnapshotId(state.snapshotDocuments);
    state.snapshotId = preferredSnapshotId;

    updateSnapshotPicker();
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
      `(${state.snapshotScope}/${captureSource}, selected=${buildSnapshotLabel(selectedDocument, { maxLength: 72 })})` +
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
    refreshExamplesDropdownSelection();
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

      const restoredId =
        state.snapshotId && state.snapshotDocuments.some((doc) => doc.id === state.snapshotId)
          ? state.snapshotId
          : choosePreferredSnapshotId(state.snapshotDocuments);

      state.snapshotId = restoredId;
      updateSnapshotPicker();
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

  function bindDropdownDismissHandlers() {
    if (!ui.snapshotDropdownBtn || !ui.snapshotDropdownMenu) return;

    ui.snapshotDropdownBtn.addEventListener("click", () => {
      toggleSnapshotDropdown();
    });

    ui.snapshotDropdownMenu.addEventListener("click", (event) => {
      const target = event.target instanceof Element ? event.target.closest(".snapshot-dropdown-item") : null;
      if (!target || !target.dataset.snapshotId) return;
      setSnapshotDropdownOpen(false);
      guarded(() => selectSnapshot(target.dataset.snapshotId));
    });

    document.addEventListener("click", (event) => {
      if (ui.snapshotPickerWrap.classList.contains("hidden")) return;
      if (ui.snapshotPickerWrap.contains(event.target)) return;
      closeSnapshotDropdown();
    });

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") {
        closeSnapshotDropdown();
        closeExamplesDropdown();
      }
    });
  }

  function bindExamplesDropdownHandlers() {
    if (!ui.examplesDropdownBtn || !ui.examplesDropdownMenu || !ui.examplesSelect) return;

    ui.examplesDropdownBtn.addEventListener("click", () => {
      toggleExamplesDropdown();
    });

    ui.examplesDropdownMenu.addEventListener("click", (event) => {
      const target = event.target instanceof Element ? event.target.closest(".examples-dropdown-item") : null;
      if (!target || typeof target.dataset.exampleValue !== "string") return;
      ui.examplesSelect.value = target.dataset.exampleValue;
      ui.examplesSelect.dispatchEvent(new Event("change", { bubbles: true }));
      closeExamplesDropdown();
    });

    document.addEventListener("click", (event) => {
      if (ui.examplesPickerWrap && ui.examplesPickerWrap.contains(event.target)) return;
      closeExamplesDropdown();
    });
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
    bindExamplesDropdownHandlers,
    bindDropdownDismissHandlers,
    toggleLint,
    toggleQueryVisibility,
    updateExamplesPicker,
    updateTokenUi
  };
}
