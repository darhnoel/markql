const path = require("path");
const { defineConfig } = require("@playwright/test");

module.exports = defineConfig({
  testDir: path.join(__dirname, "browser_plugin/tests"),
  timeout: 60_000,
  expect: {
    timeout: 10_000
  },
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  reporter: process.env.CI ? [["github"], ["html", { open: "never" }]] : "list",
  use: {
    headless: !!process.env.CI,
    trace: "on-first-retry",
    screenshot: "only-on-failure",
    video: "retain-on-failure"
  }
});
