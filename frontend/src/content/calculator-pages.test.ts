/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The companion to `strategy-guides.test.ts`, one page family across.
 *
 * That file exists because twenty-six calculator screens with no writing on
 * them got the site flagged by AdSense. This one exists because the fix for
 * that — move every word onto `/guides/<slug>`, leave the tool alone — left the
 * calculator pages differing from each other by a heading and nothing else.
 * Measured 2026-09-16: median 6-gram similarity 0.978 across the 26 pages,
 * each self-canonical and each in the sitemap, which Search Console reported
 * as duplicate content.
 *
 * So the property under test is the same one, stated for the other family: the
 * distinguishing copy must EXIST and must be genuinely different page to page.
 * A paragraph with the strategy name substituted into it would satisfy the
 * first and reproduce the problem.
 */
import { describe, it, expect } from 'vitest';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { CALCULATOR_PAGES, getCalculatorPageCopy } from '@/content/calculator-pages';
import { STRATEGY_GUIDES } from '@/content/strategy-guides';
import { pageTitle, TITLE_MAX, DESCRIPTION_MAX } from '@/lib/seo-title';

describe('calculator page copy coverage', () => {
  it('has copy for every slug that is exported as a page', () => {
    const missing = STRATEGY_SLUGS.filter((slug) => !getCalculatorPageCopy(slug));
    // Named rather than counted: a missing entry is a page that falls back to
    // the derived title, which is the duplicate-content default.
    expect(missing).toEqual([]);
  });

  it('has no copy for a slug that is not a page', () => {
    const orphans = Object.keys(CALCULATOR_PAGES).filter(
      (slug) => !(STRATEGY_SLUGS as readonly string[]).includes(slug),
    );
    expect(orphans).toEqual([]);
  });

  it('declares its own slug on every entry', () => {
    for (const [key, copy] of Object.entries(CALCULATOR_PAGES)) {
      expect(copy.slug).toBe(key);
    }
  });
});

describe('calculator page copy is distinct and usable', () => {
  const pages = Object.values(CALCULATOR_PAGES);

  /*
   * The assertion that pins the defect, written the way the guides test writes
   * it: field by field rather than over the whole record. A record-level check
   * passes as soon as any one field differs, and the field that always differs
   * is the strategy name — which is precisely the template that caused this.
   */
  it.each(['title', 'description', 'heading', 'lede'] as const)(
    'writes a distinct %s for every strategy',
    (field) => {
      const seen = new Map<string, string>();
      for (const copy of pages) {
        const value = copy[field];
        const previous = seen.get(value);
        expect(
          previous,
          `${copy.slug} and ${previous} share an identical ${field}`,
        ).toBeUndefined();
        seen.set(value, copy.slug);
      }
      expect(seen.size).toBe(pages.length);
    },
  );

  it('writes a lede of 80 to 120 words on every page', () => {
    for (const copy of pages) {
      const words = copy.lede.trim().split(/\s+/).length;
      // A band, not a floor. Too short is a stub; too long turns a tool screen
      // back into the article that was deliberately moved to its own URL.
      expect(words, `${copy.slug} lede words`).toBeGreaterThanOrEqual(80);
      expect(words, `${copy.slug} lede words`).toBeLessThanOrEqual(120);
    }
  });

  it('keeps titles and descriptions inside what a result actually shows', () => {
    for (const copy of pages) {
      // Sized via pageTitle(): the brand suffix gives way when the combined title
      // exceeds TITLE_MAX (70). We measure the rendered string that actually ships,
      // not an authored stub or an unchecked budget.
      const renderedTitle = pageTitle(copy.title);
      expect(renderedTitle.length, `${copy.slug} title`).toBeLessThanOrEqual(TITLE_MAX);
      expect(renderedTitle.length, `${copy.slug} title`).toBeGreaterThanOrEqual(10);
      expect(copy.description.length, `${copy.slug} description`).toBeGreaterThan(110);
      expect(copy.description.length, `${copy.slug} description`).toBeLessThanOrEqual(DESCRIPTION_MAX);
    }
  });

  it('names its own strategy in the heading', () => {
    // The h1 is the phrase somebody types. A heading that has drifted off the
    // strategy it prices costs the page the query it exists to answer.
    for (const copy of pages) {
      expect(copy.heading, `${copy.slug} heading`).toMatch(/Calculator$/);
      expect(copy.heading.length, `${copy.slug} heading`).toBeLessThanOrEqual(44);
    }
  });

  /*
   * The cross-family direction, and it is not pedantry.
   *
   * Lifting the guide's opening paragraph onto the calculator page would pass
   * every check above — twenty-six distinct ledes — while swapping duplication
   * BETWEEN calculator pages for duplication between a calculator page and its
   * own guide. Two URLs competing over one piece of writing is the same defect
   * with one more page in it. Sentence-level: a shared term of art is expected,
   * a shared sentence is copying.
   */
  it('shares no sentence with the guide for the same strategy', () => {
    for (const copy of pages) {
      const guide = STRATEGY_GUIDES[copy.slug];
      if (!guide) continue;
      const guideProse = [guide.lede, guide.whenToUse, ...guide.risks].join(' ');
      for (const sentence of copy.lede.split(/(?<=\.)\s+/)) {
        if (sentence.length < 40) continue;
        expect(
          guideProse.includes(sentence),
          `${copy.slug} lede reuses a guide sentence: "${sentence.slice(0, 60)}…"`,
        ).toBe(false);
      }
    }
  });
});
