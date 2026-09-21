import { test, expect, Page } from '@playwright/test';
import { STRATEGY_SLUGS } from '../src/config/strategies';

/**
 * Browser Regression Suite for Options & Futures Calculator.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Verifies core user flows against a locally served build (`out/`) or live production
 * (via BASE_URL=https://optionsandfuturescalculator.com).
 *
 * Flows covered:
 * 1. Building a position from the option chain via ticket staging and commitment.
 * 2. Arithmetic identity verification of payoff outcomes (max loss and break-even).
 * 3. Pro gate enforcement when adding multi-leg strategies (paywall upsell vs error refusal).
 * 4. Site navigation tab states, embed isolation (/widget), and 404 page handling across all routes.
 */

/**
 * Finds the first CALL strike row in the Option Chain table that carries both
 * a published implied volatility and an executable ask price (> 0).
 */
async function findActiveCallStrikeRow(page: Page) {
  const rows = page.locator('.panel:has-text("Option Chain") table.grid-table tbody tr');
  await rows.first().waitFor({ state: 'visible', timeout: 15000 });
  const count = await rows.count();

  for (let i = 0; i < count; i++) {
    const tr = rows.nth(i);
    const tds = tr.locator('td');
    const tdCount = await tds.count();
    if (tdCount >= 8) {
      const iv = (await tds.nth(2).innerText()).trim();
      const ask = (await tds.nth(5).innerText()).trim();
      if (iv !== '—' && ask !== '—' && parseFloat(ask) > 0) {
        const strike = (await tds.nth(7).innerText()).trim();
        return { row: tr, index: i, strike: parseFloat(strike), ask: parseFloat(ask) };
      }
    }
  }
  throw new Error('Could not find a CALL strike row with published IV and ask price.');
}

/**
 * Finds a subsequent CALL strike row higher than `fromIndex` that carries both
 * a published implied volatility and an executable bid price (> 0) to sell.
 */
async function findHigherCallStrikeRow(page: Page, fromIndex: number) {
  const rows = page.locator('.panel:has-text("Option Chain") table.grid-table tbody tr');
  const count = await rows.count();

  for (let i = fromIndex + 1; i < count; i++) {
    const tr = rows.nth(i);
    const tds = tr.locator('td');
    const tdCount = await tds.count();
    if (tdCount >= 8) {
      const iv = (await tds.nth(2).innerText()).trim();
      const bid = (await tds.nth(4).innerText()).trim();
      if (iv !== '—' && bid !== '—' && parseFloat(bid) > 0) {
        const strike = (await tds.nth(7).innerText()).trim();
        return { row: tr, index: i, strike: parseFloat(strike), bid: parseFloat(bid) };
      }
    }
  }
  throw new Error('Could not find a higher CALL strike row with published IV and bid price.');
}

test.describe('Options & Futures Calculator - Regression Suite', () => {

  /**
   * FLOW 1: Build a position from the option chain (primary user action).
   *
   * What it protects:
   * Verifies that clicking a quote's buy/sell action in the option chain stages the contract
   * into the order ticket rather than appending directly, and that confirming "Add long call"
   * successfully creates a 1-leg position with correct side, type, and strike in the Position panel.
   *
   * What breaking it would cost a user:
   * The option chain is the primary entry point for options traders. If clicking an option either
   * fails to populate the ticket or silently drops the leg, users cannot construct any position
   * from the live market ladder. If a leg is committed immediately without staging in the ticket,
   * users lose the ability to review and adjust quantity, fill price, or implied volatility,
   * resulting in erroneous pricing calculations and unusable workflows.
   */
  test('Flow 1: Build a position from the option chain into ticket and position panel', async ({ page }) => {
    await page.goto('/calculator/call-spread');

    // Verify initial Position panel state is empty (0 legs)
    const positionTitle = page.locator('.panel-title:has-text("Position")');
    await expect(positionTitle).toContainText('0 legs');

    // Wait for Option Chain table to render
    const activeCall = await findActiveCallStrikeRow(page);

    // Click the buy button for CALL in the selected strike row
    const buyButton = activeCall.row.locator('button[title="Buy CALL"]');
    await buyButton.click();

    // Critical assertion: clicking the chain MUST stage into Ticket, NOT append directly to Position
    await expect(positionTitle).toContainText('0 legs');

    // Verify Ticket panel offers the "Add long call" button
    const ticketPanel = page.locator('.panel:has-text("Ticket")');
    const addLegButton = ticketPanel.locator('button.btn-primary');
    await expect(addLegButton).toBeEnabled();
    await expect(addLegButton).toHaveText(/Add long call/i);

    // Commit ticket to position
    await addLegButton.click();

    // Assert Position panel increments from 0 legs to 1 leg
    await expect(positionTitle).toContainText('1 leg');

    // Assert the Position row correctly displays side, type, and strike
    const positionRow = page.locator('.panel:has-text("Position") table tbody tr').first();
    await expect(positionRow.locator('td').nth(0)).toHaveText('BUY');
    await expect(positionRow.locator('td').nth(1)).toContainText('CALL');
    await expect(positionRow.locator('td').nth(2)).toHaveText(activeCall.strike.toFixed(2));
  });

  /**
   * FLOW 2: Pricing engine outcome arithmetic identity verification.
   *
   * What it protects:
   * Verifies that the computational engine's payoff outcomes satisfy exact arithmetic identities:
   * for a single long call with strike K and premium P, max loss must equal P * 100 to the cent,
   * and break-even must equal K + P to the cent. This test asserts mathematical identities computed
   * dynamically from the page's rendered values (K and P), ensuring immunity to daily live market movements.
   *
   * What breaking it would cost a user:
   * In options trading, miscalculating risk and break-even points has direct monetary consequences.
   * If the engine calculates incorrect loss caps or break-even thresholds, traders could enter
   * mispriced trades or mismanage capital allocation based on faulty analytics.
   */
  test('Flow 2: Engine answers and satisfies arithmetic identity (max loss == P x 100, break-even == K + P)', async ({ page }) => {
    await page.goto('/calculator/call-spread');

    // Locate active call strike and add to position
    const activeCall = await findActiveCallStrikeRow(page);
    await activeCall.row.locator('button[title="Buy CALL"]').click();

    const addLegButton = page.locator('.panel:has-text("Ticket") button.btn-primary');
    await expect(addLegButton).toBeEnabled();
    await addLegButton.click();

    // Read Strike K and Premium P directly from the committed Position row
    const positionRow = page.locator('.panel:has-text("Position") table tbody tr').first();
    const strikeText = (await positionRow.locator('td').nth(2).innerText()).trim();
    const premiumInput = await positionRow.locator('td').nth(4).locator('input').inputValue();

    const K = parseFloat(strikeText);
    const P = parseFloat(premiumInput);

    expect(Number.isFinite(K)).toBe(true);
    expect(Number.isFinite(P)).toBe(true);
    expect(P).toBeGreaterThan(0);

    // Wait for Outcome panel to display computed engine result
    const outcomePanel = page.locator('.panel:has-text("Outcome")');
    await expect(outcomePanel.locator('.chip:has-text("engine")')).toBeVisible({ timeout: 15000 });

    // Read rendered Max Loss and Break-even from the Outcome stat values
    const maxLossStat = outcomePanel.locator('.stat:has-text("Max loss") .stat-value');
    const breakEvenStat = outcomePanel.locator('.stat:has-text("Break-even") .stat-value');

    await expect(maxLossStat).toBeVisible();
    await expect(breakEvenStat).toBeVisible();

    const maxLossText = (await maxLossStat.innerText()).trim();
    const breakEvenText = (await breakEvenStat.innerText()).trim();

    // Parse sanitized numerical amounts (handling currency symbol $, commas, minus sign −)
    const maxLossVal = parseFloat(maxLossText.replace(/[^0-9.]/g, ''));
    const breakEvenVal = parseFloat(breakEvenText.replace(/[^0-9.]/g, ''));

    // Assert identities to the cent
    const expectedMaxLoss = Math.round(P * 100 * 100) / 100;
    const expectedBreakEven = Math.round((K + P) * 100) / 100;

    expect(maxLossVal).toBeCloseTo(expectedMaxLoss, 2);
    expect(breakEvenVal).toBeCloseTo(expectedBreakEven, 2);
    expect(maxLossVal.toFixed(2)).toBe(expectedMaxLoss.toFixed(2));
    expect(breakEvenVal.toFixed(2)).toBe(expectedBreakEven.toFixed(2));
  });

  /**
   * FLOW 3: Pro gate entitlement enforcement and refusal rendering.
   *
   * What it protects:
   * Verifies that constructing a multi-leg strategy (adding a second leg, such as selling a higher call)
   * properly triggers the Pro entitlement gate, displaying the "Needs Pro" heading and the "Start free trial"
   * call to action, while strictly avoiding the generic error branch ("Unavailable").
   *
   * What breaking it would cost a user:
   * This protects both the business and user trust. The documented regression rendered paywall refusals
   * as broken server errors ("Unavailable") rather than an upgrade opportunity. For users, an "Unavailable"
   * error suggests the application is malfunctioning or down, leaving them frustrated without a path forward.
   * For the business, it causes lost conversion opportunities at the exact moment purchase intent is highest.
   */
  test('Flow 3: Pro gate displays Needs Pro and trial CTA on second leg, without Unavailable error', async ({ page }) => {
    await page.goto('/calculator/call-spread');

    // 1. Add first leg: Buy CALL
    const firstCall = await findActiveCallStrikeRow(page);
    await firstCall.row.locator('button[title="Buy CALL"]').click();
    const ticketAddBtn = page.locator('.panel:has-text("Ticket") button.btn-primary');
    await expect(ticketAddBtn).toBeEnabled();
    await ticketAddBtn.click();

    // Verify 1 leg added
    const positionTitle = page.locator('.panel-title:has-text("Position")');
    await expect(positionTitle).toContainText('1 leg');

    // 2. Add second leg: Sell higher CALL
    const secondCall = await findHigherCallStrikeRow(page, firstCall.index);
    await secondCall.row.locator('button[title="Sell CALL"]').click();
    await expect(ticketAddBtn).toBeEnabled();
    await expect(ticketAddBtn).toHaveText(/Add short call/i);
    await ticketAddBtn.click();

    // Verify 2 legs committed
    await expect(positionTitle).toContainText('2 legs');

    // 3. Outcome panel assertions
    const outcomePanel = page.locator('.panel:has-text("Outcome")');

    // Must show upgrade prompt heading "Needs Pro"
    const needsProHeading = outcomePanel.locator('.empty-state-title:has-text("Needs Pro")');
    await expect(needsProHeading).toBeVisible({ timeout: 15000 });

    // Must show trial call to action button "Start free trial"
    const trialCta = outcomePanel.getByRole('button', { name: 'Start free trial' });
    await expect(trialCta).toBeVisible();

    // Must NOT show the error branch ("Unavailable") - assert both directions
    const unavailableTitle = outcomePanel.locator('.empty-state-title:has-text("Unavailable")');
    await expect(unavailableTitle).toHaveCount(0);
    const unavailableText = outcomePanel.getByText('Unavailable');
    await expect(unavailableText).toHaveCount(0);
  });

  /**
   * FLOW 4: Site navigation tab states, embedding isolation, and error handling.
   *
   * What it protects:
   * Verifies that the site navigation tabs declare aria-current="page" correctly:
   * - "Calculator" active on / and every /calculator/[strategy]
   * - "Guides" active on /guides and every /guides/[strategy]
   * - Neither tab active on non-tab pages (/privacy, /terms)
   * - Navigation chrome completely suppressed on /widget (iframe embed)
   * - Navigation chrome retained and HTTP 404 status returned on non-existent routes.
   * Strategy slugs are dynamically derived from src/config/strategies.ts to prevent drift.
   *
   * What breaking it would cost a user:
   * Accessible navigation relies on accurate aria-current indicators for screen readers and visual orientation.
   * If /widget leaked site navigation, third-party sites embedding the calculator widget would display broken
   * or competing navigation chrome, violating embedding contracts and leaking user navigation out of the iframe.
   * If 404 pages broke or lost navigation, lost users would encounter dead ends with no way back to the application.
   */
  test('Flow 4: Site navigation tabs aria-current states, widget isolation, and 404 error handling', async ({ page }) => {
    const siteNav = page.locator('nav[aria-label="Site"]');
    const calcTab = siteNav.getByRole('link', { name: 'Calculator', exact: true });
    const guidesTab = siteNav.getByRole('link', { name: 'Guides', exact: true });

    // 1. Root /: Calculator active, Guides not active
    await page.goto('/');
    await expect(calcTab).toHaveAttribute('aria-current', 'page');
    await expect(guidesTab).not.toHaveAttribute('aria-current', 'page');

    // 2. All /calculator/[strategy] slugs from src/config/strategies.ts
    for (const slug of STRATEGY_SLUGS) {
      await page.goto(`/calculator/${slug}`);
      await expect(calcTab).toHaveAttribute('aria-current', 'page');
      await expect(guidesTab).not.toHaveAttribute('aria-current', 'page');
    }

    // 3. /guides index: Guides active, Calculator not active
    await page.goto('/guides');
    await expect(guidesTab).toHaveAttribute('aria-current', 'page');
    await expect(calcTab).not.toHaveAttribute('aria-current', 'page');

    // 4. All /guides/[strategy] slugs from src/config/strategies.ts
    for (const slug of STRATEGY_SLUGS) {
      await page.goto(`/guides/${slug}`);
      await expect(guidesTab).toHaveAttribute('aria-current', 'page');
      await expect(calcTab).not.toHaveAttribute('aria-current', 'page');
    }

    // 5. /privacy: neither tab active
    await page.goto('/privacy');
    await expect(calcTab).not.toHaveAttribute('aria-current', 'page');
    await expect(guidesTab).not.toHaveAttribute('aria-current', 'page');

    // 6. /terms: neither tab active
    await page.goto('/terms');
    await expect(calcTab).not.toHaveAttribute('aria-current', 'page');
    await expect(guidesTab).not.toHaveAttribute('aria-current', 'page');

    // 7. /widget: site nav chrome is completely absent (embedded iframe)
    await page.goto('/widget');
    await expect(page.locator('nav[aria-label="Site"]')).toHaveCount(0);

    // 8. 404 route: returns HTTP 404 and preserves site nav chrome
    const response = await page.goto('/this-route-does-not-exist-404');
    expect(response?.status()).toBe(404);
    await expect(page.locator('nav[aria-label="Site"]')).toBeVisible();
  });

});
