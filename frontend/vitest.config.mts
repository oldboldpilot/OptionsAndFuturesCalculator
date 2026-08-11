/**
 * Vitest configuration.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The frontend had no test tooling at all until a defect on the PRIMARY user
 * path reached production: selecting a strike and pressing Add, without
 * touching the Expiry dropdown, produced a leg with `expiration_days: 0`, and
 * every analytical panel then refused with "No expiration on any leg" while
 * that dropdown visibly showed a date. The leg was otherwise complete, so
 * nothing threw and nothing was null -- a browser was needed to see it at all.
 *
 * A unit test on `commitTicket` catches that in milliseconds. That is the point
 * of this file: the stores hold the rules that decide whether a position can be
 * priced, and those rules are plain functions over plain state. They do not
 * need a DOM, a server or a browser to be checked.
 *
 * `environment: 'node'` on purpose. Zustand stores are readable through
 * `getState()` without React, so a DOM would only add cost and a second way for
 * a test to fail for reasons unrelated to what it is testing.
 */
import { defineConfig } from 'vitest/config';
import path from 'node:path';

// `import.meta.dirname` rather than `__dirname`: this file is ESM (.mts), and
// Vite's native config loader rejects the CommonJS global.
const here = import.meta.dirname;

export default defineConfig({
  test: {
    environment: 'node',
    globals: true,
    include: ['src/**/*.test.ts'],
    // Each test file re-imports the stores to get a clean module registry;
    // isolation keeps one file's mocked RPC from leaking into the next.
    isolate: true,
  },
  resolve: {
    alias: { '@': path.resolve(here, 'src') },
  },
});
