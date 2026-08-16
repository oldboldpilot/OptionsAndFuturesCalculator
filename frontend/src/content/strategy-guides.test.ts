/**
 * @author Olumuyiwa Oluwasanmi
 *
 * These tests exist because of a specific production failure, not as coverage
 * for its own sake. On 2026-08-16 AdSense flagged this site under "Google-served
 * ads on screens without publisher-content". The cause, measured on the live
 * pages: `/calculator/long-call` and `/calculator/iron-condor` both returned
 * exactly 753 words and differed only by the strategy name — twenty-six
 * near-duplicate screens, each carrying advertising and none carrying a
 * sentence anyone could read.
 *
 * So the property under test is not "the guides exist". It is that every
 * strategy page has content AND that the content is genuinely DIFFERENT from
 * its siblings. A template filled in with a substituted name would satisfy the
 * first and reproduce the violation.
 */
import { describe, it, expect } from 'vitest';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { STRATEGY_GUIDES, getStrategyGuide } from '@/content/strategy-guides';

describe('strategy guide coverage', () => {
  it('has a guide for every slug that is exported as a page', () => {
    const missing = STRATEGY_SLUGS.filter((slug) => !getStrategyGuide(slug));
    // Named rather than counted: a failure should say WHICH page ships with no
    // content, because that page is the one serving ads against nothing.
    expect(missing).toEqual([]);
  });

  it('has no guide for a slug that is not a page', () => {
    // The reverse direction. An orphan guide is dead weight, and more to the
    // point it means the homepage index links somewhere that does not exist.
    const orphans = Object.keys(STRATEGY_GUIDES).filter(
      (slug) => !(STRATEGY_SLUGS as readonly string[]).includes(slug),
    );
    expect(orphans).toEqual([]);
  });

  it('declares its own slug on every entry', () => {
    // The record key and the `slug` field are two places the same fact is
    // written. Divergence would produce a page whose canonical URL and content
    // disagree.
    for (const [key, guide] of Object.entries(STRATEGY_GUIDES)) {
      expect(guide.slug).toBe(key);
    }
  });
});

describe('strategy guide content is substantive', () => {
  const guides = Object.values(STRATEGY_GUIDES);

  it('gives every guide a lede long enough to be publisher content', () => {
    for (const guide of guides) {
      // 200 characters is roughly two sentences. Well below what is written,
      // and well above what a placeholder would be.
      expect(guide.lede.length, `${guide.slug} lede`).toBeGreaterThan(200);
    }
  });

  it('states the three payoff identities on every guide', () => {
    for (const guide of guides) {
      expect(guide.maxProfit.length, `${guide.slug} maxProfit`).toBeGreaterThan(20);
      expect(guide.maxLoss.length, `${guide.slug} maxLoss`).toBeGreaterThan(20);
      expect(guide.breakeven.length, `${guide.slug} breakeven`).toBeGreaterThan(10);
    }
  });

  it('gives every guide construction, risks, a worked example and questions', () => {
    for (const guide of guides) {
      expect(guide.construction.length, `${guide.slug} construction`).toBeGreaterThan(0);
      expect(guide.risks.length, `${guide.slug} risks`).toBeGreaterThan(1);
      expect(guide.example.rows.length, `${guide.slug} example rows`).toBeGreaterThan(2);
      expect(guide.faqs.length, `${guide.slug} faqs`).toBeGreaterThan(0);
    }
  });

  /*
   * The one that actually pins the violation.
   *
   * Uniqueness is asserted on the prose fields individually rather than on the
   * whole record, because the record contains short enumerated fields —
   * `netCost` is one of four values and `outlook` repeats across siblings by
   * design. Those SHOULD collide. The written material must not.
   */
  it.each(['lede', 'greeks', 'whenToUse'] as const)(
    'writes a distinct %s for every strategy',
    (field) => {
      const seen = new Map<string, string>();
      for (const guide of guides) {
        const value = guide[field];
        const previous = seen.get(value);
        expect(
          previous,
          `${guide.slug} and ${previous} share an identical ${field}`,
        ).toBeUndefined();
        seen.set(value, guide.slug);
      }
      expect(seen.size).toBe(guides.length);
    },
  );

  it('does not reuse a worked example between strategies', () => {
    const setups = guides.map((g) => g.example.setup);
    expect(new Set(setups).size).toBe(setups.length);
  });
});
