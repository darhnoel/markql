function captureFrameDocument(scope) {
  function textLen(el) {
    if (!el || !el.textContent) return 0;
    return el.textContent.trim().length;
  }

  function findLargestElement(root, maxDepth) {
    let best = null;
    let bestLen = -1;

    function visit(node, depth) {
      if (!(node instanceof Element) || depth > maxDepth) return;
      const len = textLen(node);
      if (len > bestLen) {
        best = node;
        bestLen = len;
      }
      for (const child of node.children) {
        visit(child, depth + 1);
      }
    }

    for (const child of root.children) {
      visit(child, 1);
    }

    return best;
  }

  function captureMainContentHtml() {
    const preferred = document.querySelector("article, main, [role='main'], [role='full']");
    if (preferred) {
      return { html: preferred.outerHTML, source: "preferred" };
    }

    const body = document.body;
    if (body) {
      const candidate = findLargestElement(body, 3);
      if (candidate) {
        return { html: candidate.outerHTML, source: "heuristic" };
      }
    }

    if (document.documentElement) {
      return { html: document.documentElement.outerHTML, source: "full_fallback" };
    }

    return { html: "", source: "empty" };
  }

  function capture(scopeName) {
    if (scopeName === "full") {
      const html = document.documentElement ? document.documentElement.outerHTML : "";
      return { html, source: "full" };
    }
    return captureMainContentHtml();
  }

  let frameName = "";
  let frameElementId = "";
  try {
    const owner = window.frameElement;
    if (owner instanceof Element) {
      frameName = owner.getAttribute("name") || "";
      frameElementId = owner.getAttribute("id") || "";
    }
  } catch (_err) {
    // Ignore frameElement access failures and fall back to URL/title labels.
  }

  const result = capture(scope);
  return {
    html: result.html,
    source: result.source,
    url: window.location.href,
    title: document.title || "",
    frameName,
    frameElementId,
    isTop: window.top === window,
    sizeBytes: result.html.length
  };
}

async function listAllFrames(tabId) {
  if (!chrome.webNavigation || !chrome.webNavigation.getAllFrames) {
    return [];
  }
  try {
    const frames = await chrome.webNavigation.getAllFrames({ tabId });
    return Array.isArray(frames) ? frames : [];
  } catch (_err) {
    return [];
  }
}

async function captureSnapshot(tabId, scope) {
  const frameMetadata = await listAllFrames(tabId);
  const executionResults = await chrome.scripting.executeScript({
    target: { tabId, allFrames: true },
    func: captureFrameDocument,
    args: [scope]
  });
  const metadataByFrameId = new Map(frameMetadata.map((frame) => [frame.frameId, frame]));
  const documents = [];

  for (const entry of executionResults) {
    if (!entry || !entry.result || typeof entry.result.html !== "string" || !entry.result.html) {
      continue;
    }
    const meta = metadataByFrameId.get(entry.frameId);
    documents.push({
      id: `frame-${entry.frameId}`,
      frameId: entry.frameId,
      parentFrameId: meta && typeof meta.parentFrameId === "number" ? meta.parentFrameId : null,
      url: entry.result.url || (meta ? meta.url : ""),
      title: entry.result.title || "",
      frameName: entry.result.frameName || "",
      frameElementId: entry.result.frameElementId || "",
      isTop: !!entry.result.isTop,
      source: entry.result.source || "unknown",
      html: entry.result.html,
      sizeBytes: entry.result.sizeBytes || entry.result.html.length
    });
  }

  documents.sort((left, right) => {
    if (left.isTop !== right.isTop) return left.isTop ? -1 : 1;
    return left.frameId - right.frameId;
  });

  if (!documents.length) {
    throw new Error("Failed to capture page snapshot");
  }

  const capturedFrameIds = new Set(documents.map((doc) => doc.frameId));
  const inaccessibleFrames = frameMetadata
    .filter((frame) => !capturedFrameIds.has(frame.frameId))
    .map((frame) => ({
      frameId: frame.frameId,
      parentFrameId: frame.parentFrameId,
      url: frame.url || "",
      reason: "Frame document could not be captured"
    }));

  const topDocument = documents.find((doc) => doc.isTop) || documents[0];
  const totalBytes = documents.reduce((sum, doc) => sum + doc.sizeBytes, 0);

  return {
    ok: true,
    scope,
    source: documents.length > 1 ? "frame_docs" : topDocument.source,
    html: topDocument.html,
    size_bytes: totalBytes,
    documents,
    inaccessible_frames: inaccessibleFrames
  };
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
