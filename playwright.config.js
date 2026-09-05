import { defineConfig } from '@playwright/test';
export default defineConfig({
  testDir: './tests/browser',
  timeout: 45000,
  workers: 1,
  use: { baseURL: 'http://127.0.0.1:8080', channel: process.env.CI ? undefined : 'chrome', viewport: { width: 1440, height: 1100 } },
  webServer: { command: 'python3 scripts/serve.py', url: 'http://127.0.0.1:8080', reuseExistingServer: !process.env.CI },
});
