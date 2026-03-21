import { getPopupUi } from "./dom.js";
import { createPaneController } from "./panes.js";
import { createQueryEditor } from "./query-editor.js";
import { createPopupRuntime } from "./runtime.js";
import { createPopupState } from "./state.js";

export function bootPopup() {
  const ui = getPopupUi();
  const state = createPopupState();

  let runtime;
  const editor = createQueryEditor({
    ui,
    state,
    onQueryChanged: ({ recordHistory }) => {
      runtime.clearRunError();
      panes.renderErrorsPane();
      if (recordHistory) {
        editor.recordHistoryState();
      }
    },
    onRunShortcut: () => runtime.guarded(runtime.runQuery)
  });

  const panes = createPaneController({
    ui,
    state,
    getQueryText: editor.getQueryText,
    lintQuery: editor.lintQuery
  });

  runtime = createPopupRuntime({
    ui,
    state,
    editor,
    panes
  });

  ui.saveTokenBtn.addEventListener("click", () => runtime.guarded(runtime.saveToken));
  ui.editTokenBtn.addEventListener("click", () => {
    runtime.setTokenEditorVisible(true);
    runtime.updateTokenUi();
  });
  ui.cancelTokenBtn.addEventListener("click", () => {
    if (ui.tokenInput.value.trim()) {
      runtime.setTokenEditorVisible(false);
    }
    runtime.updateTokenUi();
  });
  ui.captureBtn.addEventListener("click", () => runtime.guarded(() => runtime.captureSnapshot(true)));
  ui.runBtn.addEventListener("click", () => runtime.guarded(runtime.runQuery));
  ui.toggleQueryBtn.addEventListener("click", () => runtime.guarded(runtime.toggleQueryVisibility));
  ui.runResultsBtn.addEventListener("click", () => runtime.guarded(runtime.runQuery));
  ui.examplesSelect.addEventListener("change", () => runtime.guarded(runtime.applySelectedExample));
  ui.formatBtn.addEventListener("click", () => runtime.guarded(runtime.formatCurrentQuery));
  ui.lintBtn.addEventListener("click", () => runtime.guarded(runtime.toggleLint));
  ui.copyQueryBtn.addEventListener("click", () => runtime.guarded(runtime.copyQuery));
  ui.copyExportBtn.addEventListener("click", () => runtime.guarded(runtime.copyActiveExport));

  for (const button of ui.tabButtons) {
    button.addEventListener("click", () => {
      panes.setActiveTab(button.dataset.tab);
      panes.updateCopyExportLabel();
    });
  }

  editor.bindEvents();

  editor.renderQueryHighlight();
  panes.refreshDerivedViews(editor.renderLineNumbers);
  panes.setActiveTab("table");
  runtime.restoreSettings().catch((err) => {
    panes.status(`Error: ${err && err.message ? err.message : String(err)}`, "error");
  });
}
