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
      let capturedRequest = null;
      await context.route("http://127.0.0.1:7337/v1/query", async (route) => {
        capturedRequest = JSON.parse(route.request().postData() || "{}");
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

      await popupPage.locator("#queryInput").fill(
        "SELECT ATTR(a, href)\nFROM doc\nWHERE href CONTAINS 'example.com';"
      );
      await popupPage.locator("#runBtn").click();

      await expect(popupPage.locator("#resultsTable tbody tr")).toHaveCount(2);
      await expect(popupPage.locator("#resultsTable tbody")).toContainText("https://example.com/alpha");
      await expect(popupPage.locator("#statusLine")).toContainText("rows 2");

      expect(capturedRequest).toBeTruthy();
      expect(capturedRequest.query).toContain("SELECT ATTR(a, href)");
      expect(capturedRequest.html).toContain("Fixture Page");
      expect(capturedRequest.html).toContain("https://example.com/alpha");
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
});
