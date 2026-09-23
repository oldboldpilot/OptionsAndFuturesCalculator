import { test, expect, Page } from '@playwright/test';

/**
 * Money rendering and the display-currency picker, in a real browser.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * money-format.test.ts proves the formatter and money-source-sweep.test.ts
 * proves no component bypasses it. Neither can see whether grouped text
 * actually reaches the screen, which is the only thing a user experiences —
 * and the defect that shipped was precisely that shape: the two components
 * with a local `money()` helper grouped correctly, so nothing looked broken,
 * while the ticket total rendered $7250.00 and a grid cell rendered 12775.
 *
 * Long unbroken digit runs are the discriminator throughout. Asserting an exact
 * string would pin one locale and one quote; asserting that no amount on the
 * page carries five or more consecutive digits catches every ungrouped render
 * without depending on today's market data.
 */

/**
 * An amount whose integer part has four or more consecutive digits, i.e. one
 * that is >= 1000 and carries no separator.
 *
 * FOUR, not five. The first version of this used {5,} and would NOT have
 * caught `$7250.00` -- the exact string the ticket rendered -- because 7250 is
 * four digits. A gate written a digit too loose is the defect it was meant to
 * catch, wearing the gate's clothes.
 */
const UNGROUPED_MONEY = /[$€£¥₹]\s?\d{4,}/;

async function openApp(page: Page) {
  await page.goto('/');
  await page.locator('nav[aria-label="Site"]').waitFor({ state: 'visible', timeout: 20000 });
}

test.describe('display currency picker', () => {
  test('is present in the nav and defaults to USD', async ({ page }) => {
    await openApp(page);
    const select = page.locator('.currency-select select');
    await expect(select).toBeVisible();
    await expect(select).toHaveValue('USD');
  });

  test('offers the full list once hydrated, not just the selected one', async ({ page }) => {
    // The list is client-only: rendering 38 options server-side pushed the
    // calculator pages over check-export's duplicate-content ceiling.
    await openApp(page);
    const options = page.locator('.currency-select select option');
    await expect.poll(async () => options.count(), { timeout: 15000 }).toBeGreaterThan(30);
  });

  test('discloses that amounts are not converted', async ({ page }) => {
    await openApp(page);
    const title = await page.locator('.currency-select select').getAttribute('title');
    expect(title).toMatch(/NOT converted/i);
  });

  test('changing it re-renders amounts with the new symbol', async ({ page }) => {
    await openApp(page);
    const select = page.locator('.currency-select select');
    await select.selectOption('EUR');
    await expect(select).toHaveValue('EUR');
    // The selection survives a reload, which is what makes it a preference
    // rather than a toggle.
    await page.reload();
    await page.locator('nav[aria-label="Site"]').waitFor({ state: 'visible', timeout: 20000 });
    await expect(page.locator('.currency-select select')).toHaveValue('EUR');
  });
});

test.describe('no amount renders ungrouped', () => {
  test('the calculator screen carries no run of four or more digits after a symbol', async ({
    page,
  }) => {
    await openApp(page);
    // Give the chain and the default position time to price.
    await page.waitForTimeout(6000);
    const body = await page.locator('body').innerText();
    const offenders = body.match(new RegExp(UNGROUPED_MONEY, 'g')) ?? [];
    expect(offenders).toEqual([]);
  });

  test('the regex would catch the defect that shipped', async () => {
    // A gate that cannot fail is not a gate.
    expect(UNGROUPED_MONEY.test('$7250.00')).toBe(true);
    expect(UNGROUPED_MONEY.test('$12775')).toBe(true);
    expect(UNGROUPED_MONEY.test('$7,250.00')).toBe(false);
    expect(UNGROUPED_MONEY.test('$725.00')).toBe(false);
  });
});
