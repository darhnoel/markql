const { test, expect } = require("@playwright/test");
const { launchExtensionContext, openPopupPage, startFixtureServer } = require("./helpers");

async function bindPopupToFixtureTab(popupPage, fixtureBaseUrl) {
  await popupPage.evaluate(async (targetBaseUrl) => {
    const originalQuery = chrome.tabs.query.bind(chrome.tabs);
    chrome.tabs.query = async (queryInfo = {}) => {
      if (queryInfo.active && queryInfo.currentWindow) {
        const tabs = await originalQuery({});
        const matchingTab = tabs.find((tab) => typeof tab.url === "string" && tab.url.startsWith(targetBaseUrl));
        if (matchingTab) {
          return [matchingTab];
        }
      }
      return originalQuery(queryInfo);
    };
  }, fixtureBaseUrl);
}

test.describe("browser plugin popup", () => {
  test("captures the active page and runs a query through the agent", async () => {
    const server = await startFixtureServer();
    const { context, extensionId } = await launchExtensionContext();

    try {
      const capturedRequests = [];
      await context.route("http://127.0.0.1:7337/v1/query", async (route) => {
        capturedRequests.push(JSON.parse(route.request().postData() || "{}"));
        await route.fulfill({
          status: 200,
          contentType: "application/json",
          body: JSON.stringify({
            columns: [{ name: "href" }],
            rows: [["https://example.com/alpha"], ["https://example.com/beta"]],
            elapsed_ms: 12,
            truncated: false
          })
        });
      });

      const fixturePage = await context.newPage();
      await fixturePage.goto(`${server.baseUrl}/basic-page`);

      const popupPage = await openPopupPage(context, extensionId);
      await bindPopupToFixtureTab(popupPage, server.baseUrl);

      await popupPage.locator("#tokenInput").fill("test-token");
      await popupPage.locator("#saveTokenBtn").click();

      await popupPage.locator("#captureBtn").click();
      await expect(popupPage.locator("#statusLine")).toContainText("Captured");

      const copyButton = popupPage.locator("#copyExportBtn");
      const runResultsButton = popupPage.locator("#runResultsBtn");
      await expect(copyButton).toBeVisible();
      await expect(runResultsButton).toBeVisible();
      const buttons = await popupPage.locator(".output-actions > button").evaluateAll((nodes) =>
        nodes.map((node) => node.id)
      );
      expect(buttons).toEqual(["copyExportBtn", "runResultsBtn"]);

      await popupPage.locator("#queryInput").fill(
        "SELECT ATTR(a, href)\nFROM doc\nWHERE href CONTAINS 'example.com';"
      );
      await popupPage.locator("#runBtn").click();

      await expect(popupPage.locator("#resultsTable tbody tr")).toHaveCount(2);
      await expect(popupPage.locator("#resultsTable tbody")).toContainText("https://example.com/alpha");
      await expect(popupPage.locator("#statusLine")).toContainText("rows 2");

      await runResultsButton.click();
      await expect(popupPage.locator("#resultsTable tbody tr")).toHaveCount(2);
      await expect(popupPage.locator("#statusLine")).toContainText("rows 2");

      expect(capturedRequests).toHaveLength(2);
      expect(capturedRequests[0].query).toContain("SELECT ATTR(a, href)");
      expect(capturedRequests[0].html).toContain("Fixture Page");
      expect(capturedRequests[0].html).toContain("https://example.com/alpha");
      expect(capturedRequests[1].query).toContain("SELECT ATTR(a, href)");
    } finally {
      await context.close();
      await server.close();
    }
  });

  test("supports examples, formatting, line numbers, and lint/error tabs", async () => {
    const { context, extensionId } = await launchExtensionContext();

    try {
      const popupPage = await openPopupPage(context, extensionId);

      await popupPage.selectOption("#examplesSelect", { label: "Links" });
      await expect(popupPage.locator("#queryInput")).toContainText("SELECT ATTR(a, href)");
      await expect(popupPage.locator("#lineNumbers")).toHaveText("1\n2\n3");

      await popupPage.locator("#queryInput").fill("select text(title) from doc where title contains 'Fixture'");
      await popupPage.locator("#formatBtn").click();
      await expect(popupPage.locator("#queryInput")).toContainText("SELECT");
      await expect(popupPage.locator("#queryInput")).toContainText("FROM");

      await popupPage.locator("#queryInput").fill("SELECT ATTR(a, href)\nFROM doc\nWHERE href CONTAINS 'broken");
      await popupPage.locator("#tabErrorsBtn").click();
      await expect(popupPage.locator("#errorsOutput")).toContainText("Unterminated single-quoted string");
      await expect(popupPage.locator("#copyExportBtn")).toHaveText("Copy Errors");

      await popupPage.locator("#tabJsonBtn").click();
      await expect(popupPage.locator("#copyExportBtn")).toHaveText("Copy JSON");
      await expect(popupPage.locator("#jsonOutput")).toContainText("No result yet.");
    } finally {
      await context.close();
    }
  });

  test("auto-selects the errors tab and clears stale results for agent query errors", async () => {
    const server = await startFixtureServer();
    const { context, extensionId } = await launchExtensionContext();

    try {
      await context.route("http://127.0.0.1:7337/v1/query", async (route) => {
        const body = JSON.parse(route.request().postData() || "{}");
        if (typeof body.query === "string" && body.query.includes("EXISTS(d WHERE")) {
          await route.fulfill({
            status: 200,
            contentType: "application/json",
            body: JSON.stringify({
              elapsed_ms: 1,
              columns: [],
              rows: [],
              truncated: false,
              error: {
                code: "QUERY_ERROR",
                message: "Query parse error: Expected axis name (self, parent, child, ancestor, descendant)"
              }
            })
          });
          return;
        }

        await route.fulfill({
          status: 200,
          contentType: "application/json",
          body: JSON.stringify({
            columns: [{ name: "href" }],
            rows: [["https://example.com/alpha"]],
            elapsed_ms: 9,
            truncated: false,
            error: null
          })
        });
      });

      const fixturePage = await context.newPage();
      await fixturePage.goto(`${server.baseUrl}/basic-page`);

      const popupPage = await openPopupPage(context, extensionId);
      await bindPopupToFixtureTab(popupPage, server.baseUrl);

      await popupPage.locator("#tokenInput").fill("test-token");
      await popupPage.locator("#saveTokenBtn").click();
      await popupPage.locator("#captureBtn").click();

      await popupPage.locator("#queryInput").fill("SELECT ATTR(a, href)\nFROM doc\nWHERE href CONTAINS 'example.com';");
      await popupPage.locator("#runBtn").click();
      await expect(popupPage.locator("#resultsTable tbody tr")).toHaveCount(1);

      await popupPage.locator("#queryInput").fill(
        "SELECT PROJECT(div) AS (\n" +
        "  day_num: TEXT(span)\n" +
        ")\n" +
        "FROM doc\n" +
        "WHERE tag = 'div'\n" +
        "  AND EXISTS(d WHERE class = 'mui-1y0ir9k');"
      );
      await popupPage.locator("#runBtn").click();

      await expect(popupPage.locator("#tabErrorsBtn")).toHaveAttribute("aria-selected", "true");
      await expect(popupPage.locator("#copyExportBtn")).toHaveText("Copy Errors");
      await expect(popupPage.locator("#resultsTable tbody tr")).toHaveCount(0);
      await expect(popupPage.locator("#jsonOutput")).toContainText("No result yet.");
      await expect(popupPage.locator("#statusLine")).toContainText("Parse error");
      await expect(popupPage.locator("#statusLine")).toContainText("See Errors");
      await expect(popupPage.locator("#statusLine")).toHaveClass(/error/);
      await expect(popupPage.locator("#errorsOutput")).toContainText("Expected axis name");
      await expect(popupPage.locator("#errorsOutput")).toContainText(
        "Use EXISTS(self|parent|child|ancestor|descendant WHERE ...)"
      );
      await expect(popupPage.locator("#errorsOutput")).not.toContainText("Run Error");
      await expect(popupPage.locator("#errorsOutput")).not.toContainText("QUERY_ERROR");
      await expect(popupPage.locator("#errorsOutput")).not.toContainText("No lint issues detected.");
      await expect(popupPage.locator("#errorsOutput")).not.toContainText("Current Query");
    } finally {
      await context.close();
      await server.close();
    }
  });

  test("toggles query visibility and restores the collapsed state", async () => {
    const { context, extensionId } = await launchExtensionContext();

    try {
      const popupPage = await openPopupPage(context, extensionId);
      const toggleButton = popupPage.locator("#toggleQueryBtn");
      const editorShell = popupPage.locator(".editor-shell");

      await expect(toggleButton).toHaveText("Hide");
      await expect(toggleButton).toHaveAttribute("aria-expanded", "true");
      await expect(editorShell).toBeVisible();

      await toggleButton.click();

      await expect(toggleButton).toHaveText("Show");
      await expect(toggleButton).toHaveAttribute("aria-expanded", "false");
      await expect(editorShell).toBeHidden();
      await expect(popupPage.locator("#statusLine")).toContainText("Query hidden.");

      await popupPage.reload();

      await expect(toggleButton).toHaveText("Show");
      await expect(toggleButton).toHaveAttribute("aria-expanded", "false");
      await expect(editorShell).toBeHidden();

      await toggleButton.click();

      await expect(toggleButton).toHaveText("Hide");
      await expect(toggleButton).toHaveAttribute("aria-expanded", "true");
      await expect(editorShell).toBeVisible();
    } finally {
      await context.close();
    }
  });

  test("shows a new line after one Enter at end of the query", async () => {
    const { context, extensionId } = await launchExtensionContext();

    try {
      const popupPage = await openPopupPage(context, extensionId);
      const queryInput = popupPage.locator("#queryInput");

      await queryInput.fill("SELECT 1");
      await queryInput.click();
      await popupPage.keyboard.press("End");
      await popupPage.keyboard.press("Enter");

      await expect(queryInput).toContainText("SELECT 1");
      await expect(popupPage.locator("#lineNumbers")).toHaveText("1\n2");

      const editorState = await popupPage.locator("#queryInput").evaluate((node) => ({
        text: node.textContent,
        html: node.innerHTML
      }));
      expect(editorState.text).toBe("SELECT 1\n");
      expect(editorState.html).toContain("<br>");
    } finally {
      await context.close();
    }
  });
});
