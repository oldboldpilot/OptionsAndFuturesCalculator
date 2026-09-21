import { defineConfig, devices } from '@playwright/test';

/**
 * Playwright browser regression test suite configuration.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Designed to run browser-level regression tests against:
 * 1. Local static build: `next build` static export into `out/`, served via `npx serve -l 3000 -n out`.
 *    (Note: `next start` does not work with Next.js static exports (`output: 'export'`) and throws
 *    an explicit error directing use of `serve out` instead).
 * 2. Production: pointed at https://optionsandfuturescalculator.com by setting `BASE_URL`.
 *
 * Does NOT run as part of `npm test` (vitest) or `npm run build` to keep unit testing fast
 * and browser-free.
 */

const baseURL = process.env.BASE_URL || 'http://localhost:3000';

export default defineConfig({
  testDir: './e2e',
  fullyParallel: false,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: 1,
  reporter: [['list']],
  timeout: 45000,
  expect: {
    timeout: 10000,
  },
  use: {
    baseURL,
    trace: 'on-first-retry',
    viewport: { width: 1280, height: 800 },
  },
  projects: [
    {
      name: 'chromium',
      use: { ...devices['Desktop Chrome'] },
    },
  ],
  webServer: process.env.BASE_URL
    ? undefined
    : {
        command: 'npx serve -l 3000 -n out',
        url: 'http://localhost:3000',
        reuseExistingServer: !process.env.CI,
        timeout: 15000,
      },
});
