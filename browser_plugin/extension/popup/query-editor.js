import { SQL_FUNCTIONS, SQL_KEYWORDS } from "./config.js";

export function createQueryEditor({ ui, state, onQueryChanged, onRunShortcut }) {
  let suppressHistoryPush = false;
  let historyIndex = -1;
  const queryHistory = [];

  function escapeHtml(text) {
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function highlightSql(query) {
    let i = 0;
    const out = [];

    const pushToken = (cls, text) => {
      out.push(`<span class="${cls}">${escapeHtml(text)}</span>`);
    };

    while (i < query.length) {
      const ch = query[i];

      if (ch === "-" && query[i + 1] === "-") {
        let j = i + 2;
        while (j < query.length && query[j] !== "\n") j += 1;
        pushToken("sql-comment", query.slice(i, j));
        i = j;
        continue;
      }

      if (ch === "/" && query[i + 1] === "*") {
        let j = i + 2;
        while (j + 1 < query.length && !(query[j] === "*" && query[j + 1] === "/")) j += 1;
        j = Math.min(query.length, j + 2);
        pushToken("sql-comment", query.slice(i, j));
        i = j;
        continue;
      }

      if (ch === "'") {
        let j = i + 1;
        while (j < query.length) {
          if (query[j] === "'") {
            if (query[j + 1] === "'") {
              j += 2;
              continue;
            }
            j += 1;
            break;
          }
          j += 1;
        }
        pushToken("sql-str", query.slice(i, j));
        i = j;
        continue;
      }

      if (/\s/.test(ch)) {
        out.push(escapeHtml(ch));
        i += 1;
        continue;
      }

      if ("(),.;".includes(ch)) {
        pushToken("sql-op", ch);
        i += 1;
        continue;
      }

      if (/[<>~=:+\-*/]/.test(ch)) {
        let j = i + 1;
        while (j < query.length && /[<>~=:+\-*/]/.test(query[j])) j += 1;
        pushToken("sql-op", query.slice(i, j));
        i = j;
        continue;
      }

      if (/[0-9]/.test(ch)) {
        let j = i + 1;
        while (j < query.length && /[0-9.]/.test(query[j])) j += 1;
        pushToken("sql-num", query.slice(i, j));
        i = j;
        continue;
      }

      if (/[A-Za-z_]/.test(ch)) {
        let j = i + 1;
        while (j < query.length && /[A-Za-z0-9_]/.test(query[j])) j += 1;
        const word = query.slice(i, j);
        const upper = word.toUpperCase();
        if (SQL_KEYWORDS.has(upper)) {
          pushToken("sql-kw", word);
        } else if (SQL_FUNCTIONS.has(upper)) {
          pushToken("sql-fn", word);
        } else {
          out.push(escapeHtml(word));
        }
        i = j;
        continue;
      }

      out.push(escapeHtml(ch));
      i += 1;
    }

    return out.join("");
  }

  function getQueryText() {
    return ui.queryInput ? ui.queryInput.textContent || "" : "";
  }

  function setQueryText(text) {
    ui.queryInput.textContent = text || "";
  }

  function getSelectionOffsets(container) {
    const selection = window.getSelection();
    if (!selection || selection.rangeCount === 0) return { start: 0, end: 0 };
    const range = selection.getRangeAt(0);
    if (!container.contains(range.startContainer) || !container.contains(range.endContainer)) {
      const length = getQueryText().length;
      return { start: length, end: length };
    }

    const startRange = range.cloneRange();
    startRange.selectNodeContents(container);
    startRange.setEnd(range.startContainer, range.startOffset);

    const endRange = range.cloneRange();
    endRange.selectNodeContents(container);
    endRange.setEnd(range.endContainer, range.endOffset);

    const start = startRange.toString().length;
    const end = endRange.toString().length;
    return start <= end ? { start, end } : { start: end, end: start };
  }

  function setSelectionOffsets(container, startOffset, endOffset) {
    const selection = window.getSelection();
    if (!selection) return;

    const range = document.createRange();
    const walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT);
    const startTarget = Math.max(0, startOffset);
    const endTarget = Math.max(0, endOffset);
    let currentOffset = 0;
    let startNode = null;
    let endNode = null;
    let startNodeOffset = 0;
    let endNodeOffset = 0;
    let node = walker.nextNode();

    while (node) {
      const length = node.nodeValue.length;
      if (!startNode && startTarget <= currentOffset + length) {
        startNode = node;
        startNodeOffset = startTarget - currentOffset;
      }
      if (!endNode && endTarget <= currentOffset + length) {
        endNode = node;
        endNodeOffset = endTarget - currentOffset;
        break;
      }
      currentOffset += length;
      node = walker.nextNode();
    }

    if (!startNode || !endNode) {
      range.selectNodeContents(container);
      range.collapse(false);
    } else {
      range.setStart(startNode, startNodeOffset);
      range.setEnd(endNode, endNodeOffset);
    }
    selection.removeAllRanges();
    selection.addRange(range);
  }

  function setCaretOffset(container, offset) {
    setSelectionOffsets(container, offset, offset);
  }

  function renderLineNumbers() {
    const query = getQueryText();
    const count = Math.max(1, query.split("\n").length);
    const numbers = [];
    for (let i = 1; i <= count; i += 1) {
      numbers.push(String(i));
    }
    ui.lineNumbers.textContent = numbers.join("\n");
  }

  function syncEditorScroll() {
    ui.lineNumbers.scrollTop = ui.queryInput.scrollTop;
  }

  function renderQueryHighlight(preserveCaret = false) {
    if (!ui.queryInput || state.isComposingQuery) return;
    const query = getQueryText();
    const scrollTop = ui.queryInput.scrollTop;
    const selection = preserveCaret ? getSelectionOffsets(ui.queryInput) : { start: 0, end: 0 };
    let rendered = highlightSql(query);
    if (query.endsWith("\n")) {
      rendered += "<br>";
    }
    ui.queryInput.innerHTML = rendered;
    if (!query) {
      ui.queryInput.innerHTML = "";
    }
    renderLineNumbers();
    if (preserveCaret) {
      setSelectionOffsets(ui.queryInput, selection.start, selection.end);
    }
    ui.queryInput.scrollTop = scrollTop;
    syncEditorScroll();
  }

  function replaceQueryText(nextText, selectionStart = null, selectionEnd = null) {
    setQueryText(nextText);
    renderQueryHighlight();
    const fallback = nextText.length;
    const start = selectionStart === null ? fallback : selectionStart;
    const end = selectionEnd === null ? start : selectionEnd;
    setSelectionOffsets(ui.queryInput, start, end);
  }

  function recordHistoryState() {
    if (suppressHistoryPush) return;
    const query = getQueryText();
    const selection = getSelectionOffsets(ui.queryInput);
    const current = queryHistory[historyIndex];
    if (current && current.text === query && current.start === selection.start && current.end === selection.end) {
      return;
    }
    queryHistory.splice(historyIndex + 1);
    queryHistory.push({ text: query, start: selection.start, end: selection.end });
    historyIndex = queryHistory.length - 1;
  }

  function restoreHistoryState(stateSnapshot) {
    suppressHistoryPush = true;
    replaceQueryText(stateSnapshot.text, stateSnapshot.start, stateSnapshot.end);
    onQueryChanged({ preserveCaret: false, recordHistory: false });
    suppressHistoryPush = false;
  }

  function undoQueryEdit() {
    if (historyIndex <= 0) return;
    historyIndex -= 1;
    restoreHistoryState(queryHistory[historyIndex]);
  }

  function redoQueryEdit() {
    if (historyIndex >= queryHistory.length - 1) return;
    historyIndex += 1;
    restoreHistoryState(queryHistory[historyIndex]);
  }

  function insertTextAtSelection(text) {
    const query = getQueryText();
    const selection = getSelectionOffsets(ui.queryInput);
    const nextText = query.slice(0, selection.start) + text + query.slice(selection.end);
    const nextOffset = selection.start + text.length;
    replaceQueryText(nextText, nextOffset, nextOffset);
    onQueryChanged({ preserveCaret: false, recordHistory: true });
  }

  function indentSelectedLines(unindent = false) {
    const query = getQueryText();
    const selection = getSelectionOffsets(ui.queryInput);
    if (selection.start === selection.end && !unindent) {
      insertTextAtSelection("  ");
      return;
    }
    const lineStart = query.lastIndexOf("\n", Math.max(0, selection.start - 1)) + 1;
    const lineEndBoundary = selection.end < query.length ? query.indexOf("\n", selection.end) : -1;
    const lineEnd = lineEndBoundary === -1 ? query.length : lineEndBoundary;
    const block = query.slice(lineStart, lineEnd);
    const lines = block.split("\n");

    let removedBeforeStart = 0;
    let removedWithinSelection = 0;
    const updatedLines = lines.map((line, index) => {
      if (!unindent) return `  ${line}`;
      let removeCount = 0;
      if (line.startsWith("  ")) {
        removeCount = 2;
      } else if (line.startsWith("\t") || line.startsWith(" ")) {
        removeCount = 1;
      }
      if (index === 0) removedBeforeStart = removeCount;
      removedWithinSelection += removeCount;
      return line.slice(removeCount);
    });

    const replacement = updatedLines.join("\n");
    const nextText = query.slice(0, lineStart) + replacement + query.slice(lineEnd);

    let nextStart;
    let nextEnd;
    if (!unindent) {
      nextStart = selection.start + 2;
      nextEnd = selection.end + 2 * lines.length;
    } else {
      nextStart = Math.max(lineStart, selection.start - removedBeforeStart);
      nextEnd = Math.max(nextStart, selection.end - removedWithinSelection);
    }

    replaceQueryText(nextText, nextStart, nextEnd);
    onQueryChanged({ preserveCaret: false, recordHistory: true });
  }

  function focusQueryInput(placeCaretAtEnd = false) {
    ui.queryInput.focus();
    if (placeCaretAtEnd) {
      setCaretOffset(ui.queryInput, getQueryText().length);
    }
  }

  function formatQueryText(query) {
    let formatted = query.replace(/\r\n?/g, "\n").trim();
    if (!formatted) return "";

    for (const keyword of SQL_KEYWORDS) {
      const pattern = new RegExp(`\\b${keyword}\\b`, "gi");
      formatted = formatted.replace(pattern, keyword);
    }
    for (const fn of SQL_FUNCTIONS) {
      const pattern = new RegExp(`\\b${fn}\\b`, "gi");
      formatted = formatted.replace(pattern, fn);
    }

    formatted = formatted
      .replace(/[ \t]+\n/g, "\n")
      .replace(/\n[ \t]+/g, "\n")
      .replace(/[ \t]{2,}/g, " ");

    const breakKeywords = ["FROM", "WHERE", "LIMIT", "TO"];
    for (const keyword of breakKeywords) {
      const pattern = new RegExp(`\\s+${keyword}\\b`, "gi");
      formatted = formatted.replace(pattern, `\n${keyword}`);
    }
    formatted = formatted.replace(/\s+ORDER\s+BY\b/gi, "\nORDER BY");
    formatted = formatted.replace(/\s+(AND|OR)\b/gi, "\n  $1");
    formatted = formatted.replace(/\n{3,}/g, "\n\n");

    if (!/;\s*$/.test(formatted)) {
      formatted += ";";
    }
    return formatted;
  }

  function unterminatedSingleQuote(query) {
    let inCommentLine = false;
    let inCommentBlock = false;

    for (let i = 0; i < query.length; i += 1) {
      const ch = query[i];
      const next = query[i + 1];

      if (inCommentLine) {
        if (ch === "\n") inCommentLine = false;
        continue;
      }
      if (inCommentBlock) {
        if (ch === "*" && next === "/") {
          inCommentBlock = false;
          i += 1;
        }
        continue;
      }
      if (ch === "-" && next === "-") {
        inCommentLine = true;
        i += 1;
        continue;
      }
      if (ch === "/" && next === "*") {
        inCommentBlock = true;
        i += 1;
        continue;
      }
      if (ch === "'") {
        i += 1;
        while (i < query.length) {
          if (query[i] === "'") {
            if (query[i + 1] === "'") {
              i += 2;
              continue;
            }
            break;
          }
          i += 1;
        }
        if (i >= query.length) {
          return true;
        }
      }
    }

    return false;
  }

  function hasBalancedParens(query) {
    let depth = 0;
    let inString = false;
    let inCommentLine = false;
    let inCommentBlock = false;

    for (let i = 0; i < query.length; i += 1) {
      const ch = query[i];
      const next = query[i + 1];

      if (inCommentLine) {
        if (ch === "\n") inCommentLine = false;
        continue;
      }
      if (inCommentBlock) {
        if (ch === "*" && next === "/") {
          inCommentBlock = false;
          i += 1;
        }
        continue;
      }
      if (inString) {
        if (ch === "'") {
          if (next === "'") {
            i += 1;
          } else {
            inString = false;
          }
        }
        continue;
      }
      if (ch === "-" && next === "-") {
        inCommentLine = true;
        i += 1;
        continue;
      }
      if (ch === "/" && next === "*") {
        inCommentBlock = true;
        i += 1;
        continue;
      }
      if (ch === "'") {
        inString = true;
        continue;
      }
      if (ch === "(") depth += 1;
      if (ch === ")") depth -= 1;
      if (depth < 0) return false;
    }

    return depth === 0;
  }

  function lintQuery(query) {
    const messages = [];
    const trimmed = query.trim();

    if (!trimmed) {
      messages.push({ level: "error", text: "Query is empty." });
      return messages;
    }
    if (!/\bselect\b/i.test(trimmed)) {
      messages.push({ level: "error", text: "Missing SELECT clause." });
    }
    if (!/\bfrom\b/i.test(trimmed)) {
      messages.push({ level: "error", text: "Missing FROM clause." });
    }
    if (unterminatedSingleQuote(trimmed)) {
      messages.push({ level: "error", text: "Unterminated single-quoted string." });
    }
    if (!hasBalancedParens(trimmed)) {
      messages.push({ level: "error", text: "Unbalanced parentheses." });
    }
    if (!/;\s*$/.test(trimmed)) {
      messages.push({ level: "warning", text: "Query does not end with a semicolon." });
    }
    return messages;
  }

  function bindEvents() {
    ui.sqlEditor.addEventListener("mousedown", (event) => {
      if (event.button !== 0) return;
      if (event.target === ui.queryInput || ui.queryInput.contains(event.target)) return;
      event.preventDefault();
      focusQueryInput(true);
    });

    ui.queryInput.addEventListener("scroll", syncEditorScroll);
    ui.queryInput.addEventListener("input", () => {
      onQueryChanged({ preserveCaret: true, recordHistory: true });
    });
    ui.queryInput.addEventListener("compositionstart", () => {
      state.isComposingQuery = true;
    });
    ui.queryInput.addEventListener("compositionend", () => {
      state.isComposingQuery = false;
      onQueryChanged({ preserveCaret: true, recordHistory: true });
    });
    ui.queryInput.addEventListener("keydown", (event) => {
      if (event.isComposing || state.isComposingQuery) return;

      if ((event.metaKey || event.ctrlKey) && !event.altKey && event.key.toLowerCase() === "z") {
        event.preventDefault();
        if (event.shiftKey) {
          redoQueryEdit();
        } else {
          undoQueryEdit();
        }
        return;
      }
      if ((event.metaKey || event.ctrlKey) && !event.altKey && event.key.toLowerCase() === "y") {
        event.preventDefault();
        redoQueryEdit();
        return;
      }
      if ((event.metaKey || event.ctrlKey) && event.key === "Enter") {
        event.preventDefault();
        onRunShortcut();
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        insertTextAtSelection("\n");
        return;
      }
      if (event.key === "Tab") {
        event.preventDefault();
        indentSelectedLines(event.shiftKey);
      }
    });
    ui.queryInput.addEventListener("paste", (event) => {
      event.preventDefault();
      const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
      if (text) {
        insertTextAtSelection(text);
      }
    });
  }

  function resetHistory() {
    queryHistory.length = 0;
    historyIndex = -1;
    recordHistoryState();
  }

  return {
    bindEvents,
    focusQueryInput,
    formatQueryText,
    getQueryText,
    lintQuery,
    recordHistoryState,
    renderLineNumbers,
    renderQueryHighlight,
    resetHistory,
    setQueryText,
    syncEditorScroll
  };
}
