import { test, expect, type Page } from '@playwright/test';
import { installApiMock, type MockHandle } from './support/api-mock';

/**
 * Flow-level checks for the usability fixes, against a mocked engine.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * layout.spec.ts holds the geometry. This file holds what each fix has to DO:
 * a futures position that prices, a chain chip that tells the truth about the
 * market, a calculator page that opens on its own strategy, an embed with no
 * site chrome, an ad slot that gives its space back, and a keyboard path that
 * does not start forty-four stops from the ticket.
 */

const panel = (page: Page, title: string) =>
  page.locator('.panel', { has: page.locator('.panel-title', { hasText: new RegExp(`^${title}`) }) }).first();

// Tuesday 2026-10-06 11:00 ET: the US session is open.
const OPEN = Date.parse('2026-10-06T15:00:00Z');
// Wednesday 2026-10-07 01:12 ET: closed. The moment the LIVE chip was caught lying.
const CLOSED = Date.parse('2026-10-07T05:12:00Z');

let mock: MockHandle;

async function openAt(page: Page, now: number, path = '/') {
  // The page's own clock is pinned to the same instant as the engine's, so the
  // chain's age and the exchange calendar are both a statement about `now`.
  await page.clock.install({ time: now });
  mock = await installApiMock(page, { now, pro: false });
  await page.goto(path);
  await page.clock.resume();
}

test.describe('chain chip (OFC-014)', () => {
  test.use({ viewport: { width: 1440, height: 900 } });

  test('says MARKET CLOSED with the last quote time when the exchange is shut', async ({ page }) => {
    await openAt(page, CLOSED);
    const head = panel(page, 'Option Chain');
    await expect(head).toContainText('MARKET CLOSED');
    await expect(head).toContainText('last quote');
    await expect(head).toContainText('ET');
    await expect(head.getByText('LIVE', { exact: true })).toHaveCount(0);
  });

  test('says LIVE only when the session is open and the print is fresh', async ({ page }) => {
    await openAt(page, OPEN);
    const head = panel(page, 'Option Chain');
    await expect(head.getByText('LIVE', { exact: true })).toBeVisible();
    await expect(head).not.toContainText('MARKET CLOSED');
  });
});

test.describe('futures (OFC-007)', () => {
  test.use({ viewport: { width: 1440, height: 900 } });

  test('a futures outright prices with no implied volatility, on its own ticket', async ({ page }) => {
    await openAt(page, OPEN);
    await page.locator('[aria-label="Quick symbol select"] button[title^="ES"]').click();
    await expect(panel(page, 'Futures ticket')).toBeVisible();
    // The option ticket is gone: no strike, no premium, no IV to be asked for.
    await expect(panel(page, 'Ticket')).toHaveCount(0);
    await expect(page.getByText('IV %')).toHaveCount(0);

    await panel(page, 'Futures ticket').locator('button.btn-primary').click();
    await expect(panel(page, 'Position').locator('.panel-title')).toContainText('1 leg');

    const strip = page.locator('[data-testid="results-strip"]');
    await expect(strip.locator('[data-result="max-profit"]')).toHaveText('Unlimited', { timeout: 15000 });
    // The whole point: no "Not priced yet ... enter an IV" anywhere.
    await expect(page.getByText(/publish no implied volatility/i)).toHaveCount(0);
    await expect(page.getByText('Not priced yet')).toHaveCount(0);

    // Only the odds need a volatility, and that panel says so.
    await expect(panel(page, 'Probability Distribution')).toContainText('Needs a volatility');

    // And the leg went to the engine as dollars per point, not index points.
    const sent = mock.calculations.at(-1)!.getLegsList()[0];
    expect(sent.getContractMultiplier()).toBe(50);
    expect(mock.calculations.at(-1)!.getImpliedVolatility()).toBe(0);
  });
});

test.describe('a calculator page opens on its own strategy (OFC-027)', () => {
  test.use({ viewport: { width: 1440, height: 900 } });

  test('/calculator/iron-condor has the Iron Condor selected and previewed', async ({ page }) => {
    await openAt(page, OPEN, '/calculator/iron-condor');
    // The list entries, not the category pills above them (also aria-pressed).
    const picked = panel(page, 'Strategy').locator('.panel-body button[aria-pressed="true"]');
    await expect(picked).toHaveCount(1);
    await expect(picked).toContainText('Iron Condor');
    // Previewed, not applied: the position is still empty until Apply.
    await expect(panel(page, 'Position').locator('.panel-title')).toContainText('0 legs');
  });

  test('a futures page opens on a futures symbol', async ({ page }) => {
    await openAt(page, OPEN, '/calculator/futures-outright');
    await expect(page.locator('[aria-label="Quick symbol select"] button[title^="ES"]')).toHaveAttribute('aria-pressed', 'true');
    await expect(panel(page, 'Strategy').locator('.panel-body button[aria-pressed="true"]')).toContainText('Futures Outright Long');
  });
});

test.describe('the embeddable widget (OFC-029)', () => {
  test('carries none of the site chrome and has a heading', async ({ page }) => {
    await openAt(page, OPEN, '/widget');
    await expect(page.getByRole('heading', { level: 1 })).toHaveCount(1);
    for (const chrome of ['Where to trade these', 'All Strategy Calculators', 'Not investment advice']) {
      await expect(page.getByText(chrome)).toHaveCount(0);
    }
    await expect(page.locator('nav[aria-label="Site"]')).toHaveCount(0);
    // A calculator-sized frame, not a 2,000px page of footer.
    const height = await page.evaluate(() => document.documentElement.scrollHeight);
    expect(height).toBeLessThan(1500);
    // And no sign-in form inside somebody else's iframe.
    await expect(page.locator('input[type="password"]')).toHaveCount(0);
  });
});

test.describe('advertising slot (OFC-028)', () => {
  test.use({ viewport: { width: 1280, height: 800 } });

  test('an unfilled slot gives its space back', async ({ page }) => {
    await openAt(page, OPEN, '/guides/long-call');
    // The ad script is blocked by the mock (every foreign origin is), which is
    // the "never filled" case; the slot reserves its box and then releases it.
    await expect(page.locator('aside[aria-label="Sponsored"]')).toHaveCount(0, { timeout: 10000 });
    // The article is untouched.
    await expect(page.getByRole('heading', { level: 1 })).toContainText('Long Call');
  });

  test('the guide links to the calculator right after the payoff summary', async ({ page }) => {
    await openAt(page, OPEN, '/guides/long-call');
    const cta = page.getByRole('link', { name: /price a long call on live market data/i }).first();
    await expect(cta).toBeVisible();
    // Within the first two screens, not 2,600px down.
    const box = await cta.boundingBox();
    expect(box!.y).toBeLessThan(1600);
  });

  test('the Calculator tab follows the guide being read', async ({ page }) => {
    await openAt(page, OPEN, '/guides/iron-condor');
    await expect(page.locator('nav[aria-label="Site"]').getByRole('link', { name: 'Calculator', exact: true })).toHaveAttribute('href', '/calculator/iron-condor');
  });
});

test.describe('keyboard path (OFC-021)', () => {
  test.use({ viewport: { width: 1440, height: 900 } });

  test('the first Tab stop is a skip link, and it lands in <main>', async ({ page }) => {
    await openAt(page, OPEN, '/guides/long-call');
    await page.keyboard.press('Tab');
    const skip = page.getByRole('link', { name: /skip to main content/i });
    await expect(skip).toBeFocused();
    await expect(skip).toBeInViewport();
    await page.keyboard.press('Enter');
    await expect(page.locator('main#main')).toBeFocused();
  });

  test('the option chain is one tab stop, not hundreds', async ({ page }) => {
    await openAt(page, OPEN);
    await panel(page, 'Option Chain').locator('tbody tr').first().waitFor();
    const reachable = await panel(page, 'Option Chain').locator('tbody button:not([tabindex="-1"])').count();
    expect(reachable).toBe(1);
    // ...and the arrow keys move within it.
    const first = panel(page, 'Option Chain').locator('tbody button:not([tabindex="-1"])');
    await first.focus();
    const row = await first.getAttribute('data-nav-row');
    await page.keyboard.press('ArrowDown');
    const moved = page.locator('tbody button:focus');
    await expect(moved).toHaveAttribute('data-nav-row', String(Number(row) + 1));
  });
});
