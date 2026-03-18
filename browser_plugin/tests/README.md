# Browser Plugin Playwright Tests

This suite exercises the unpacked Chromium extension locally with Playwright.

## Coverage

- Opens the real extension UI from `browser_plugin/extension/`
- Serves a local HTML fixture page for deterministic capture tests
- Mocks the local MarkQL agent at `http://127.0.0.1:7337/v1/query`
- Verifies popup/editor behavior and basic query execution flow

## Setup

Install the JS test dependency:

```bash
npm install
```

Install the Playwright browser:

```bash
npx playwright install chromium
```

## Run

```bash
npm run test:browser-plugin
```

Run headed:

```bash
npm run test:browser-plugin:headed
```
