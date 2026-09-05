import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir: './tests/browser',
  timeout: 45000,
  workers: 1,
  use: {
    baseURL: 'http://127.0.0.1:8080',
    channel: process.env.CI ? undefined : 'chrome',
    // Linux headless-shell captures the pointer but does not deliver relative
    // mouse movement from Playwright. CI uses a desktop browser under Xvfb.
    headless: !process.env.CI,
    viewport: { width: 1440, height: 1100 },
  },
  webServer: { command: 'python3 scripts/serve.py', url: 'http://127.0.0.1:8080', reuseExistingServer: !process.env.CI },
});
