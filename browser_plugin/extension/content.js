(() => {
  if (window.__markqlContentCaptureInstalled) {
    return;
  }
  window.__markqlContentCaptureInstalled = true;

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

  function capture(scope) {
    if (scope === "full") {
      const html = document.documentElement ? document.documentElement.outerHTML : "";
      return { html, source: "full" };
    }
    return captureMainContentHtml();
  }

  chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
    if (!message || (message.type !== "markql_capture" && message.type !== "xsql_capture")) {
      return;
    }
    try {
      const scope = message.scope === "full" ? "full" : "main";
      const result = capture(scope);
      sendResponse({
        ok: true,
        scope,
        source: result.source,
        html: result.html,
        size_bytes: result.html.length
      });
    } catch (err) {
      sendResponse({
        ok: false,
        error: err && err.message ? err.message : String(err)
      });
    }
  });
})();
