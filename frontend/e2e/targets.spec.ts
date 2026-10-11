import { test, expect, type Page } from '@playwright/test';
import { installApiMock } from './support/api-mock';

/**
 * Pointer-target size, measured on the rendered page (OFC-020, WCAG 2.5.8).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Lighthouse's `target-size` audit named the chain's B/S buttons (19x19), the
 * example arrows (16.6x18.8), the segment items and the quantity box; on a phone
 * 700 of 712 interactive elements were under 44px. 2.5.8 sets 24x24 CSS pixels
 * as the AA floor, with exceptions for inline links in running text and for a
 * target whose 24px circle clears its neighbours. This checks every button,
 * select, input and summary on the screen against 24x24 outright -- the
 * exceptions are never invoked, which is stricter than the criterion -- and, on a
 * coarse pointer, against the 44px a thumb needs for the controls a visitor uses
 * to build a position.
 */

const panel = (page: Page, title: string) =>
  page.locator('.panel', { has: page.locator('.panel-title', { hasText: new RegExp(`^${title}`) }) }).first();

async function undersized(page: Page, min: number) {
  return page.evaluate((floor) => {
    const out: string[] = [];
    const seen = new Set<string>();
    for (const el of Array.from(document.querySelectorAll('button, select, input:not([type="hidden"]), summary, [role="tab"]'))) {
      const he = el as HTMLElement;
      const r = he.getBoundingClientRect();
      if (r.width === 0 || r.height === 0) continue;
      const cs = getComputedStyle(he);
      if (cs.visibility === 'hidden') continue;
      // Ignore what is scrolled out of every ancestor's box: not a target a
      // visitor can press right now.
      let hidden = false;
      for (let n: HTMLElement | null = he.parentElement; n; n = n.parentElement) {
        const o = getComputedStyle(n);
        if (o.display === 'none') { hidden = true; break; }
      }
      if (hidden) continue;
      if (r.width + 0.05 < floor || r.height + 0.05 < floor) {
        const label = he.getAttribute('aria-label') || (he.textContent ?? '').trim().slice(0, 20) || he.tagName;
        const key = `${he.tagName}|${label}`;
        if (seen.has(key)) continue;
        seen.add(key);
        out.push(`${he.tagName.toLowerCase()} "${label}" ${Math.round(r.width * 10) / 10}x${Math.round(r.height * 10) / 10}`);
      }
    }
    return out;
  }, min);
}

async function build(page: Page) {
  await installApiMock(page, { now: Date.parse('2026-10-06T15:00:00Z'), pro: false });
  await page.goto('/');
  const chain = panel(page, 'Option Chain');
  await chain.locator('tbody tr').first().waitFor({ state: 'attached' });
  const tab = page.getByRole('tab', { name: 'Chain' });
  if (await tab.isVisible().catch(() => false)) await tab.click();
  await chain.locator('tbody tr').nth(60).locator('button[title="Buy CALL"]').click();
  const build = page.getByRole('tab', { name: 'Build' });
  if (await build.isVisible().catch(() => false)) await build.click();
  await panel(page, 'Ticket').locator('button.btn-primary').click();
  await expect(panel(page, 'Position').locator('[data-testid="leg-row"]')).toHaveCount(1);
  await page.waitForTimeout(600);
}

test.describe('desktop: nothing under 24x24', () => {
  test.use({ viewport: { width: 1440, height: 900 } });
  test('every button, select, input and tab', async ({ page }) => {
    await build(page);
    expect(await undersized(page, 24)).toEqual([]);
  });

  // Lighthouse 13 measures the page a first-time visitor sees, at 1350px, and
  // found the matrix's "$" segment 23.7px wide: a width that is the glyph's own
  // plus padding, so it depends on the machine's font. Playwright's bundled
  // Chromium renders it at 24 or more, so THIS test does not reproduce that
  // shortfall -- the stylesheet's own min-width on .segment-item is what fixes it,
  // and Lighthouse's desktop accessibility score (96 -> 100) is what confirmed it.
  // What this adds is the first screen, which the test above never looks at, and
  // a tolerance of 0.05px in place of the 0.5px that would have admitted 23.7.
  test('the first screen, before any leg exists, at the width Lighthouse uses', async ({ page }) => {
    await page.setViewportSize({ width: 1350, height: 940 });
    await installApiMock(page, { now: Date.parse('2026-10-06T15:00:00Z'), pro: false });
    await page.goto('/');
    await panel(page, 'Option Chain').locator('tbody tr').first().waitFor({ state: 'attached' });
    expect(await undersized(page, 24)).toEqual([]);
  });
});

test.describe('phone: the controls a thumb uses are at least 44px', () => {
  test.use({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  test('every button, select, input and tab on the build screen', async ({ page }) => {
    await build(page);
    const bad = await undersized(page, 24);
    expect(bad).toEqual([]);
    // The primary actions, specifically, at thumb size.
    for (const loc of [
      panel(page, 'Ticket').locator('button.btn-primary'),
      page.getByRole('tab', { name: 'Results' }),
      panel(page, 'Position').locator('[aria-label^="Remove"]').first(),
    ]) {
      const box = await loc.boundingBox();
      expect(box!.height).toBeGreaterThanOrEqual(43.5);
    }
  });
});
