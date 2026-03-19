const fs = require("fs");
const http = require("http");
const os = require("os");
const path = require("path");
const { chromium } = require("@playwright/test");

async function launchExtensionContext() {
  const extensionPath = path.resolve(__dirname, "../extension");
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), "markql-playwright-"));

  const context = await chromium.launchPersistentContext(userDataDir, {
    channel: "chromium",
    headless: false,
    args: [
      `--disable-extensions-except=${extensionPath}`,
      `--load-extension=${extensionPath}`
    ]
  });

  let [serviceWorker] = context.serviceWorkers();
  if (!serviceWorker) {
    serviceWorker = await context.waitForEvent("serviceworker");
  }

  const extensionId = new URL(serviceWorker.url()).host;
  return { context, extensionId };
}

function startFixtureServer() {
  const fixturePath = path.resolve(__dirname, "fixtures/basic-page.html");
  const html = fs.readFileSync(fixturePath, "utf8");

  return new Promise((resolve, reject) => {
    const server = http.createServer((req, res) => {
      if (!req.url || req.url === "/" || req.url.startsWith("/basic-page")) {
        res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
        res.end(html);
        return;
      }
      res.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
      res.end("Not found");
    });

    server.on("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      resolve({
        baseUrl: `http://127.0.0.1:${address.port}`,
        close: () => new Promise((done, fail) => server.close((err) => (err ? fail(err) : done())))
      });
    });
  });
}

async function openPopupPage(context, extensionId) {
  const page = await context.newPage();
  await page.goto(`chrome-extension://${extensionId}/popup.html`);
  return page;
}

module.exports = {
  launchExtensionContext,
  openPopupPage,
  startFixtureServer
};
