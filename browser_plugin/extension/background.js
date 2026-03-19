async function ensureContentScript(tabId) {
  await chrome.scripting.executeScript({
    target: { tabId },
    files: ["content.js"]
  });
}

function sendMessageToTab(tabId, message) {
  return new Promise((resolve, reject) => {
    chrome.tabs.sendMessage(tabId, message, (response) => {
      if (chrome.runtime.lastError) {
        reject(new Error(chrome.runtime.lastError.message));
        return;
      }
      resolve(response);
    });
  });
}

async function captureSnapshot(tabId, scope) {
  await ensureContentScript(tabId);
  const response = await sendMessageToTab(tabId, { type: "markql_capture", scope });
  if (!response || !response.ok || typeof response.html !== "string") {
    throw new Error(response && response.error ? response.error : "Failed to capture page snapshot");
  }
  return response;
}

chrome.action.onClicked.addListener(async (tab) => {
  if (!chrome.sidePanel || !chrome.sidePanel.open || !tab || typeof tab.id !== "number") {
    return;
  }
  try {
    await chrome.sidePanel.open({ tabId: tab.id });
  } catch (err) {
    console.warn("Failed to open side panel:", err);
  }
});

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (!message || message.type !== "captureSnapshot") {
    return false;
  }

  const tabId = Number(message.tabId);
  const scope = message.scope === "full" ? "full" : "main";

  captureSnapshot(tabId, scope)
    .then((payload) => {
      sendResponse({ ok: true, ...payload });
    })
    .catch((err) => {
      sendResponse({ ok: false, error: err.message || String(err) });
    });

  return true;
});
