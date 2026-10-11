import { test, expect, type Page } from '@playwright/test';
import { installApiMock } from './support/api-mock';

/**
 * Text contrast, measured in a browser, in both themes (OFC-019, OFC-008).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Lighthouse found the ink-400 hints at 3.97-4.28:1 and accent chips at
 * 3.89-4.28:1, and the audit measured slate at 3.0-3.9:1; it also found the
 * sign-in links at about 1.03:1. None of those is visible in source: every one is
 * a design token composited over another token, sometimes over a translucent
 * tint. So the check resolves what a user actually sees -- the computed text
 * colour against the EFFECTIVE background, composited through every ancestor --
 * and holds it to WCAG AA's 4.5:1 for text under 18px.
 *
 * Elements painted with a gradient are skipped (their colour under the text is
 * not one value), as are disabled controls, which WCAG exempts.
 */

async function worstContrasts(page: Page) {
  return page.evaluate(() => {
    type RGBA = { r: number; g: number; b: number; a: number };
    // Any CSS colour the browser understands, read back as RGBA. Computed styles
    // come out as rgb(), color(srgb ...), oklab(...) or lab(...) depending on how
    // the colour was written (Tailwind's `text-white/70` is a color-mix), and a
    // hand-written parser that skips the formats it does not know turns this whole
    // check into a vacuous pass -- which is exactly what it did on its first run
    // against the page that had 1.03:1 sign-in links. The browser's own canvas
    // converts them all; painting over black and over white solves for alpha.
    const cv = document.createElement('canvas');
    cv.width = cv.height = 1;
    const ctx = cv.getContext('2d', { willReadFrequently: true })!;
    const paint = (c: string, under: string) => {
      ctx.fillStyle = under;
      ctx.fillRect(0, 0, 1, 1);
      ctx.fillStyle = c;
      ctx.fillRect(0, 0, 1, 1);
      return ctx.getImageData(0, 0, 1, 1).data;
    };
    const parse = (c: string): RGBA | null => {
      if (!c || c === 'transparent') return { r: 0, g: 0, b: 0, a: 0 };
      const onBlack = paint(c, '#000000');
      const onWhite = paint(c, '#ffffff');
      const a = 1 - (onWhite[0] - onBlack[0]) / 255;
      if (a <= 0.001) return { r: 0, g: 0, b: 0, a: 0 };
      return { r: onBlack[0] / a, g: onBlack[1] / a, b: onBlack[2] / a, a: Math.min(1, a) };
    };
    const over = (top: RGBA, bottom: RGBA): RGBA => {
      const a = top.a + bottom.a * (1 - top.a);
      if (a === 0) return { r: 0, g: 0, b: 0, a: 0 };
      const mix = (t: number, b: number) => (t * top.a + b * bottom.a * (1 - top.a)) / a;
      return { r: mix(top.r, bottom.r), g: mix(top.g, bottom.g), b: mix(top.b, bottom.b), a };
    };
    const lum = (c: RGBA) => {
      const f = (v: number) => { v /= 255; return v <= 0.03928 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; };
      return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
    };
    const ratio = (a: RGBA, b: RGBA) => {
      const [hi, lo] = [lum(a), lum(b)].sort((x, y) => y - x);
      return (hi + 0.05) / (lo + 0.05);
    };
    /** Effective opaque background of an element, or null when a gradient is in the way. */
    const backdrop = (el: Element): RGBA | null => {
      const chain: Element[] = [];
      for (let n: Element | null = el; n; n = n.parentElement) chain.push(n);
      let acc: RGBA = { r: 255, g: 255, b: 255, a: 1 };
      for (const n of chain.reverse()) {
        const cs = getComputedStyle(n);
        if (cs.backgroundImage !== 'none' && n !== document.body) return null;
        const bg = parse(cs.backgroundColor);
        if (bg && bg.a > 0) acc = over(bg, acc);
      }
      return acc;
    };

    const out: { sel: string; text: string; ratio: number; fg: string }[] = [];
    // EVERY element that owns visible text, not a list of classes somebody
    // thought of: a list would have skipped `text-white/70`, the one that was
    // 1.03:1, because no one had named it.
    for (const el of Array.from(document.body.querySelectorAll('*'))) {
      if (['SCRIPT', 'STYLE', 'NOSCRIPT', 'SVG', 'PATH'].includes(el.tagName.toUpperCase())) continue;
      const own = Array.from(el.childNodes)
        .filter((n) => n.nodeType === Node.TEXT_NODE)
        .map((n) => (n.textContent ?? '').trim())
        .join(' ')
        .trim();
      if (!own) continue;
      const he = el as HTMLElement;
      if ((he as HTMLButtonElement).disabled) continue;
      const r = he.getBoundingClientRect();
      if (r.width === 0 || r.height === 0) continue;
      const cs = getComputedStyle(he);
      if (cs.visibility === 'hidden' || Number(cs.opacity) < 1) continue;
      // Anything inside a faded ancestor is exempt for the same reason.
      let faded = false;
      for (let n: Element | null = he; n; n = n.parentElement) {
        if (Number(getComputedStyle(n).opacity) < 1) { faded = true; break; }
      }
      if (faded) continue;
      const fg = parse(cs.color);
      const bg = backdrop(he);
      if (!fg || !bg) continue;
      const solidFg = over(fg, bg);
      out.push({
        sel: `${he.tagName.toLowerCase()}${he.className && typeof he.className === 'string' ? '.' + he.className.split(/\s+/).slice(0, 2).join('.') : ''}`,
        text: own.slice(0, 32),
        ratio: Math.round(ratio(solidFg, bg) * 100) / 100,
        fg: cs.color,
      });
    }
    // One line per distinct (element, text) -- the chain alone has 240 identical
    // S buttons, and a failure listing that is 240 copies of one defect hides the
    // other six.
    const distinct = new Map<string, (typeof out)[number]>();
    // Keeping the WORST of each, not the first: the chain's rows alternate
    // between two backgrounds, and a first-wins dedupe reports the white row,
    // which passes, and hides the banded row, which does not.
    for (const c of out) {
      const k = `${c.sel}|${c.text}`;
      const prev = distinct.get(k);
      if (!prev || c.ratio < prev.ratio) distinct.set(k, c);
    }
    return [...distinct.values()].sort((a, b) => a.ratio - b.ratio);
  });
}

for (const theme of ['light', 'slate'] as const) {
  test.describe(`${theme} theme`, () => {
    test.use({ viewport: { width: 1440, height: 900 } });
    test.beforeEach(async ({ page }) => {
      await page.addInitScript((t) => {
        try { localStorage.setItem('ofc-theme', t); localStorage.setItem('ofc-theme-v2', '1'); } catch { /* storage may be blocked */ }
      }, theme);
      await installApiMock(page, { now: Date.parse('2026-10-06T15:00:00Z'), pro: false });
    });

    test('the calculator, with a position, has no text under 4.5:1', async ({ page }) => {
      await page.goto('/');
      const chain = page.locator('.panel', { has: page.locator('.panel-title', { hasText: /^Option Chain/ }) }).first();
      await chain.locator('tbody tr').nth(60).locator('button[title="Buy CALL"]').click();
      await page.locator('.panel', { has: page.locator('.panel-title', { hasText: /^Ticket/ }) }).first().locator('button.btn-primary').click();
      await expect(page.locator('[data-testid="results-strip"] [data-result="max-loss"]')).toBeVisible({ timeout: 15000 });
      await page.waitForTimeout(800);
      const bad = (await worstContrasts(page)).filter((c) => c.ratio < 4.5);
      expect(bad, JSON.stringify(bad.slice(0, 12), null, 1)).toEqual([]);
    });

    test('the sign-in page is legible and its fields have a visible edge', async ({ page }) => {
      await page.goto('/auth/sign-in');
      const bad = (await worstContrasts(page)).filter((c) => c.ratio < 4.5);
      expect(bad, JSON.stringify(bad.slice(0, 12), null, 1)).toEqual([]);

      // A field boundary needs 3:1 against what is behind it (WCAG 1.4.11). The
      // inputs were `border-white/20`: invisible on a light page.
      const edge = await page.locator('input[type="email"]').evaluate((el) => {
        const cs = getComputedStyle(el);
        return { width: cs.borderTopWidth, style: cs.borderTopStyle, color: cs.borderTopColor };
      });
      expect(edge.style).toBe('solid');
      expect(parseFloat(edge.width)).toBeGreaterThan(0);

      // The links the page tells you to use must exist as real, labelled controls.
      await expect(page.getByRole('button', { name: 'Create account' })).toBeVisible();
      await expect(page.getByRole('button', { name: 'Forgot password?' })).toBeVisible();
      await expect(page.getByLabel('Email')).toBeVisible();
      await expect(page.getByLabel('Password')).toBeVisible();
    });
  });
}

test('the sign-in form is centred, not flush to the left edge (OFC-008)', async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await installApiMock(page, { pro: false });
  await page.goto('/auth/sign-in');
  const box = await page.locator('main h1').boundingBox();
  // `mx-auto` was being overridden by an unlayered `margin: 0` reset.
  expect(box!.x).toBeGreaterThan(300);
});

test('the reset page does not accuse a visitor with no link of using an expired one (OFC-008)', async ({ page }) => {
  await installApiMock(page, { pro: false });
  await page.goto('/auth/reset-password');
  await expect(page.getByLabel('Your email')).toBeVisible();
  await expect(page.getByText(/no longer valid/i)).toHaveCount(0);
});
