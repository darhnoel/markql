export function createPopupState() {
  return {
    snapshotHtml: "",
    snapshotScope: "",
    snapshotDocuments: [],
    snapshotId: "",
    lastResult: null,
    lastRunError: null,
    isComposingQuery: false,
    lintEnabled: true,
    queryCollapsed: false,
    activeOutputTab: "table"
  };
}
