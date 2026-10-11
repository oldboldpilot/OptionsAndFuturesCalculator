import { test, expect, type Page } from '@playwright/test';
import { installApiMock, type MockHandle } from './support/api-mock';

/**
 * Layout assertions for the calculator workspace, against a mocked engine.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Lighthouse scored this page 98-100 on desktop while the numbers the tool
 * exists to show sat 200px past the right edge of the screen. A performance or
 * accessibility score cannot see WHERE a value is drawn, so these tests assert
 * geometry directly: every headline figure is inside the viewport, the centre
 * column is wide enough to read, the strike ladder shows its strikes, and a
 * phone is not a desktop layout panned sideways.
 *
 * The engine is mocked (see support/api-mock.ts) so a red result means the page
 * is wrong, never that the market was closed or the entitlement gate was on.
 */

const DESKTOPS = [
  { width: 1280, height: 800 },
  { width: 1440, height: 900 },
  { width: 1920, height: 1080 },
];

let mock: MockHandle;

/**
 * A panel by its TITLE. `.panel:has-text("Option Chain")` is not that: :has-text
 * is a case-insensitive substring match over everything inside, so it also
 * matched the Position panel's "Select strikes from the option chain..." and
 * the strike-visibility check measured the wrong box.
 */
const panel = (page: Page, title: string) =>
  page.locator('.panel', { has: page.locator('.panel-title', { hasText: new RegExp(`^${title}`) }) }).first();

async function open(page: Page, path = '/') {
  mock = await installApiMock(page, { pro: false });
  await page.goto(path);
  // 'attached', not 'visible': on a phone the chain lives in a tab that is not
  // showing yet, and waiting for it to be visible would wait forever.
  await panel(page, 'Option Chain').locator('tbody tr').first().waitFor({ state: 'attached', timeout: 20000 });
}

/** On a phone the three areas are tabs; on a desktop there is no tab bar. */
async function showArea(page: Page, name: 'Build' | 'Chain' | 'Results') {
  const tab = page.getByRole('tab', { name });
  if (await tab.isVisible().catch(() => false)) await tab.click();
}

/** Buy the at-the-money call from the chain and commit it to the position. */
async function addAtmLongCall(page: Page) {
  await showArea(page, 'Chain');
  const atm = panel(page, 'Option Chain').locator('tbody tr').nth(60);
  await atm.locator('button[title="Buy CALL"]').click();
  await showArea(page, 'Build');
  await panel(page, 'Ticket').locator('button.btn-primary').click();
  await expect(panel(page, 'Position').locator('.panel-title')).toContainText('1 leg');
  await showArea(page, 'Results');
  await expect(panel(page, 'Outcome').locator('.stat-value').first()).toBeVisible({ timeout: 15000 });
}

const inViewport = (box: { x: number; y: number; width: number; height: number } | null, vw: number) =>
  box !== null && box.x >= -0.5 && box.x + box.width <= vw + 0.5;

for (const size of DESKTOPS) {
  test.describe(`desktop ${size.width}x${size.height}`, () => {
    test.use({ viewport: size });

    test('every Outcome value is inside the viewport after adding a leg (OFC-001)', async ({ page }) => {
      await open(page);
      await addAtmLongCall(page);

      const values = panel(page, 'Outcome').locator('.stat-value');
      const count = await values.count();
      expect(count).toBeGreaterThan(6);
      for (let i = 0; i < count; i++) {
        const box = await values.nth(i).boundingBox();
        expect(inViewport(box, size.width), `Outcome value #${i} at ${JSON.stringify(box)}`).toBe(true);
      }
    });

    test('the pinned results strip shows the headline numbers without scrolling (OFC-001)', async ({ page }) => {
      await open(page);
      await addAtmLongCall(page);

      for (const key of ['max-profit', 'max-loss', 'break-even', 'pop']) {
        const el = page.locator(`[data-testid="results-strip"] [data-result="${key}"]`);
        await expect(el).toBeVisible();
        const box = await el.boundingBox();
        expect(box, key).not.toBeNull();
        expect(inViewport(box, size.width), `${key} ${JSON.stringify(box)}`).toBe(true);
        expect(box!.y >= 0 && box!.y + box!.height <= size.height, `${key} is within the first screen`).toBe(true);
        expect((await el.innerText()).trim().length, `${key} has text`).toBeGreaterThan(0);
      }
    });

    test('the centre column is at least 420px wide (OFC-006)', async ({ page }) => {
      await open(page);
      const box = await panel(page, 'Probability Distribution').boundingBox();
      expect(box).not.toBeNull();
      expect(box!.width).toBeGreaterThanOrEqual(420);
    });

    test('the option chain shows its STRIKE column (OFC-004)', async ({ page }) => {
      await open(page);
      const scroller = panel(page, 'Option Chain').locator('.panel-body');
      const head = panel(page, 'Option Chain').locator('th', { hasText: 'STRIKE' }).first();
      const s = await scroller.boundingBox();
      const h = await head.boundingBox();
      expect(s && h).toBeTruthy();
      expect(h!.x).toBeGreaterThanOrEqual(s!.x - 0.5);
      expect(h!.x + h!.width).toBeLessThanOrEqual(s!.x + s!.width + 0.5);
    });

    test('the quantity box shows its value and is labelled (OFC-005)', async ({ page }) => {
      await open(page);
      await addAtmLongCall(page);
      const qty = panel(page, 'Position').locator('input[type="number"]').first();
      await expect(qty).toHaveValue('1');
      const m = await qty.evaluate((el) => {
        const cs = getComputedStyle(el);
        const ctx = document.createElement('canvas').getContext('2d')!;
        ctx.font = `${cs.fontSize} ${cs.fontFamily}`;
        const content = el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
        return { content, text: ctx.measureText((el as HTMLInputElement).value).width, label: el.getAttribute('aria-label') };
      });
      // Room for the digit AND the native spinner (about 15px), which is what
      // ate the value when the box was 22px wide.
      expect(m.content).toBeGreaterThanOrEqual(m.text + 15);
      expect(m.label).toMatch(/quantity/i);
    });
  });
}

test.describe('phone 390x844', () => {
  test.use({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });

  test('the document does not scroll sideways (OFC-002)', async ({ page }) => {
    await open(page);
    const m = await page.evaluate(() => ({
      doc: document.documentElement.scrollWidth,
      client: document.documentElement.clientWidth,
      body: document.body.scrollWidth,
    }));
    expect(m.doc).toBeLessThanOrEqual(m.client);
    expect(m.body).toBeLessThanOrEqual(m.client);
  });

  test('Build, Chain and Results are tabs, and Results shows the numbers (OFC-002)', async ({ page }) => {
    await open(page);
    await expect(page.getByRole('tablist')).toBeVisible();
    for (const name of ['Build', 'Chain', 'Results']) {
      await expect(page.getByRole('tab', { name })).toBeVisible();
    }
    await addAtmLongCall(page);
    const strip = page.locator('[data-testid="results-strip"] [data-result="max-loss"]');
    await expect(strip).toBeVisible();
    const box = await strip.boundingBox();
    expect(inViewport(box, 390)).toBe(true);

    // Still no sideways scroll once a position exists.
    const m = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(m).toBeLessThanOrEqual(0);
  });

  test('the STRIKE column stays on screen however far the ladder is scrolled (OFC-004)', async ({ page }) => {
    await open(page);
    await showArea(page, 'Chain');
    const body = panel(page, 'Option Chain').locator('.panel-body');
    const head = panel(page, 'Option Chain').locator('th.chain-strike', { hasText: 'STRIKE' }).first();

    // The compact phone ladder is 628px in a 372px panel, and the strike sits
    // in the middle of it, so at every scroll position it is on screen anyway:
    // the sticky cell is not what keeps it there at THIS width. It is what keeps
    // it there at any narrower one (a 320px phone, a split-screen window, a
    // user's own zoom), so squeeze the scroller until the table is far wider than
    // its window. Without that this test passed with `position: sticky` deleted.
    await body.evaluate((el) => {
      el.style.width = '200px';
      el.style.maxWidth = '200px';
      el.style.flex = 'none';
    });
    const spare = await body.evaluate((el) => el.scrollWidth - el.clientWidth);
    expect(spare, 'the chain must be wider than its window here, or there is nothing to scroll').toBeGreaterThan(300);

    for (const where of ['left', 'right', 'middle'] as const) {
      await body.evaluate((el, w) => {
        el.scrollLeft = w === 'left' ? 0 : w === 'right' ? el.scrollWidth : (el.scrollWidth - el.clientWidth) / 2;
      }, where);
      const s = await body.boundingBox();
      const h = await head.boundingBox();
      expect(s && h, where).toBeTruthy();
      expect(h!.x, `${where}: STRIKE starts inside the panel`).toBeGreaterThanOrEqual(s!.x - 0.5);
      expect(h!.x + h!.width, `${where}: STRIKE ends inside the panel`).toBeLessThanOrEqual(s!.x + s!.width + 0.5);
    }
  });

  test('every Results panel has real width, not a 2px sliver (OFC-002)', async ({ page }) => {
    await open(page);
    await addAtmLongCall(page);
    for (const title of ['Probability Distribution', 'P&L matrix', 'Outcome']) {
      const box = await panel(page, title).boundingBox();
      expect(box, title).not.toBeNull();
      expect(box!.width, title).toBeGreaterThanOrEqual(300);
    }
  });
});

test.describe('laptop 1024x768 (OFC-006)', () => {
  test.use({ viewport: { width: 1024, height: 768 } });

  test('the build column never disappears behind a tab, and the first leg shows the results', async ({ page }) => {
    await open(page);

    // Two tabs here, not three: Build is the column on the left, always shown.
    await expect(page.getByRole('tab', { name: 'Build' })).toBeHidden();
    await expect(page.getByRole('tab', { name: 'Chain' })).toHaveAttribute('aria-selected', 'true');
    await expect(panel(page, 'Ticket')).toBeVisible();
    await expect(panel(page, 'Position')).toBeVisible();

    // The bug this pins: choosing a tab hid the build column too, because the
    // phone rule that hides areas by tab was more specific than the laptop rule
    // that was meant to override it.
    await page.getByRole('tab', { name: 'Results' }).click();
    await expect(panel(page, 'Ticket')).toBeVisible();
    await page.getByRole('tab', { name: 'Chain' }).click();
    await expect(panel(page, 'Ticket')).toBeVisible();

    // A price clicked in the chain fills the ticket, which is on screen with it.
    await panel(page, 'Option Chain').locator('tbody tr').nth(60).locator('button[title="Buy CALL"]').click();
    await expect(panel(page, 'Ticket').locator('button.btn-primary')).toBeInViewport();
    await panel(page, 'Ticket').locator('button.btn-primary').click();

    // ...and the answer arrives without a second click.
    await expect(page.getByRole('tab', { name: 'Results' })).toHaveAttribute('aria-selected', 'true');
    const box = await panel(page, 'Probability Distribution').boundingBox();
    expect(box!.width).toBeGreaterThanOrEqual(420);
    for (const key of ['max-profit', 'max-loss', 'break-even', 'pop']) {
      expect(inViewport(await page.locator(`[data-testid="results-strip"] [data-result="${key}"]`).boundingBox(), 1024), key).toBe(true);
    }
  });
});

test.describe('200% zoom: a 720px viewport (OFC-006, WCAG 1.4.4)', () => {
  test.use({ viewport: { width: 720, height: 450 } });

  test('keeps real width on every results panel and does not scroll sideways', async ({ page }) => {
    await open(page);
    await addAtmLongCall(page);
    for (const title of ['Probability Distribution', 'P&L matrix', 'Outcome']) {
      const box = await panel(page, title).boundingBox();
      expect(box!.width, title).toBeGreaterThanOrEqual(300);
    }
    const over = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
    expect(over).toBeLessThanOrEqual(0);
  });
});

test.describe('symbol switch with a position (OFC-003)', () => {
  test.use({ viewport: { width: 1440, height: 900 } });

  test('does not silently re-price the old legs under the new symbol', async ({ page }) => {
    await open(page);
    await addAtmLongCall(page);
    const before = mock.calculations.length;
    expect(before).toBeGreaterThan(0);

    await page.locator('button[title^="NVDA"]').click();
    await expect(page.locator('[aria-label="Quick symbol select"] button[title^="NVDA"]')).toHaveAttribute('aria-pressed', 'true');

    // The SPY leg is not carried across: the position is empty and says why.
    await expect(panel(page, 'Position').locator('.panel-title')).toContainText('0 legs');
    await expect(page.getByRole('status').filter({ hasText: /SPY/ })).toBeVisible();
    await expect(page.getByRole('button', { name: /undo/i })).toBeVisible();

    // And nothing was sent to the engine pairing SPY legs with NVDA.
    for (const req of mock.calculations.slice(before)) {
      expect(req.getUnderlyingSymbol()).not.toBe('NVDA');
    }

    // Undo puts the position back, under the symbol it belongs to.
    await page.getByRole('button', { name: /undo/i }).click();
    await expect(panel(page, 'Position').locator('.panel-title')).toContainText('1 leg');
    await expect(page.locator('[aria-label="Quick symbol select"] button[title^="SPY"]')).toHaveAttribute('aria-pressed', 'true');
  });
});
