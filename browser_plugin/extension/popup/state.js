export function createPopupState() {
  return {
    snapshotHtml: "",
    snapshotScope: "",
    lastResult: null,
    lastRunError: null,
    isComposingQuery: false,
    lintEnabled: true,
    queryCollapsed: false,
    activeOutputTab: "table"
  };
}
