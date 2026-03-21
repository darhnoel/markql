export function createPaneController({ ui, state, getQueryText, lintQuery }) {
  function status(text, tone = "info") {
    ui.statusLine.textContent = text;
    ui.statusLine.classList.toggle("error", tone === "error");
  }

  function applyQueryCollapsedUi() {
    ui.workspace.classList.toggle("query-collapsed", state.queryCollapsed);
    ui.toggleQueryBtn.textContent = state.queryCollapsed ? "Show" : "Hide";
    ui.toggleQueryBtn.setAttribute("aria-expanded", state.queryCollapsed ? "false" : "true");
  }

  function valueToText(value) {
    if (value === null || value === undefined) return "";
    if (typeof value === "object") return JSON.stringify(value);
    return String(value);
  }

  function csvEscape(value) {
    const text = valueToText(value);
    if (text.includes('"') || text.includes(",") || text.includes("\n") || text.includes("\r")) {
      return '"' + text.replace(/"/g, '""') + '"';
    }
    return text;
  }

  function buildCsv(columns, rows) {
    const header = columns.map((c) => csvEscape(c.name));
    const lines = [header.join(",")];
    for (const row of rows) {
      lines.push(row.map((value) => csvEscape(value)).join(","));
    }
    return lines.join("\n");
  }

  function buildJson(columns, rows) {
    const objects = rows.map((row) => {
      const obj = {};
      for (let i = 0; i < columns.length; i += 1) {
        obj[columns[i].name] = i < row.length ? row[i] : null;
      }
      return obj;
    });
    return JSON.stringify(objects, null, 2);
  }

  function clearResultsTable() {
    ui.resultsHead.innerHTML = "";
    ui.resultsBody.innerHTML = "";
  }

  function renderErrorsPane() {
    const query = getQueryText();
    const lintMessages = state.lintEnabled ? lintQuery(query) : [];
    ui.errorsOutput.innerHTML = "";

    const hasErrorState = !!state.lastRunError || lintMessages.some((m) => m.level === "error");
    const shouldShowLintSection = !state.lastRunError || !state.lintEnabled || lintMessages.length > 0;

    if (state.lastRunError) {
      const section = document.createElement("section");
      section.className = "errors-section";

      const card = document.createElement("div");
      card.className = "errors-card";

      const heading = document.createElement("p");
      heading.className = "errors-card-title";
      heading.textContent = state.lastRunError.summary;
      card.appendChild(heading);

      const message = document.createElement("p");
      message.className = "errors-card-message";
      message.textContent = state.lastRunError.detail;
      card.appendChild(message);

      if (state.lastRunError.hint) {
        const hint = document.createElement("p");
        hint.className = "errors-card-hint";
        hint.textContent = `Hint: ${state.lastRunError.hint}`;
        card.appendChild(hint);
      }

      section.appendChild(card);
      ui.errorsOutput.appendChild(section);
    }

    if (shouldShowLintSection) {
      const lintSection = document.createElement("section");
      lintSection.className = "errors-section";

      const lintTitle = document.createElement("h3");
      lintTitle.className = "errors-title";
      lintTitle.textContent = "Lint";
      lintSection.appendChild(lintTitle);

      if (!state.lintEnabled) {
        const note = document.createElement("p");
        note.className = "errors-note";
        note.textContent = "Lint is off.";
        lintSection.appendChild(note);
      } else if (lintMessages.length === 0) {
        const note = document.createElement("p");
        note.className = "errors-note";
        note.textContent = "No lint issues detected.";
        lintSection.appendChild(note);
      } else {
        const list = document.createElement("ul");
        list.className = "errors-list";
        for (const lintMessage of lintMessages) {
          const item = document.createElement("li");
          item.className = `errors-item ${lintMessage.level}`;

          const header = document.createElement("div");
          header.className = "errors-item-header";

          const badge = document.createElement("span");
          badge.className = "errors-badge";
          badge.textContent = lintMessage.level;
          header.appendChild(badge);

          const itemTitle = document.createElement("span");
          itemTitle.className = "errors-item-title";
          itemTitle.textContent = lintMessage.level === "error" ? "Fix before running" : "Review before running";
          header.appendChild(itemTitle);
          item.appendChild(header);

          const body = document.createElement("div");
          body.className = "errors-item-message";
          body.textContent = lintMessage.text;
          item.appendChild(body);

          list.appendChild(item);
        }
        lintSection.appendChild(list);
      }

      ui.errorsOutput.appendChild(lintSection);
    }

    ui.errorsOutput.classList.remove("empty");
    ui.errorsOutput.classList.toggle("error", hasErrorState);
  }

  function renderJsonPane() {
    if (!state.lastResult || !Array.isArray(state.lastResult.columns) || !Array.isArray(state.lastResult.rows)) {
      ui.jsonOutput.textContent = "No result yet.";
      ui.jsonOutput.classList.add("empty");
      return;
    }
    ui.jsonOutput.textContent = buildJson(state.lastResult.columns, state.lastResult.rows);
    ui.jsonOutput.classList.remove("empty");
  }

  function renderResultsTable(result) {
    clearResultsTable();

    const headTr = document.createElement("tr");
    for (const col of result.columns) {
      const th = document.createElement("th");
      th.textContent = col.name;
      headTr.appendChild(th);
    }
    ui.resultsHead.appendChild(headTr);

    for (const row of result.rows) {
      const tr = document.createElement("tr");
      for (const value of row) {
        const td = document.createElement("td");
        td.textContent = valueToText(value);
        tr.appendChild(td);
      }
      ui.resultsBody.appendChild(tr);
    }
  }

  function setActiveTab(tabName) {
    state.activeOutputTab = tabName;
    for (const button of ui.tabButtons) {
      const active = button.dataset.tab === tabName;
      button.classList.toggle("active", active);
      button.setAttribute("aria-selected", active ? "true" : "false");
    }
    for (const pane of ui.tabPanes) {
      pane.classList.toggle("active", pane.dataset.pane === tabName);
    }
  }

  function updateCopyExportLabel() {
    if (state.activeOutputTab === "table") {
      ui.copyExportBtn.textContent = "Copy CSV";
    } else if (state.activeOutputTab === "json") {
      ui.copyExportBtn.textContent = "Copy JSON";
    } else {
      ui.copyExportBtn.textContent = "Copy Errors";
    }
  }

  function refreshDerivedViews(renderLineNumbers) {
    renderLineNumbers();
    renderJsonPane();
    renderErrorsPane();
    updateCopyExportLabel();
  }

  return {
    applyQueryCollapsedUi,
    buildCsv,
    buildJson,
    clearResultsTable,
    refreshDerivedViews,
    renderErrorsPane,
    renderJsonPane,
    renderResultsTable,
    setActiveTab,
    status,
    updateCopyExportLabel
  };
}
