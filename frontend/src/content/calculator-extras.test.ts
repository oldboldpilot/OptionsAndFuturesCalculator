/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Gates `calculator-extras.ts`, the second wave of per-strategy content added
 * below the lede on `/calculator/<slug>` — see that file's header for why it
 * exists: with only the lede shipped, the 26 pages still measured a pairwise
 * median 6-gram similarity of 0.768 on the live site.
 *
 * Two properties matter here, and they are different from `calculator-pages`'s
 * own test: the ARITHMETIC has to be right (a wrong number on a public payoff
 * table is worse than no table), and the new prose has to be genuinely
 * distinct — from its 25 siblings AND from both the lede and the guide for the
 * same strategy.
 */
import { describe, it, expect } from 'vitest';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { CALCULATOR_EXTRAS, getCalculatorExtra } from '@/content/calculator-extras';
import { STRATEGY_GUIDES } from '@/content/strategy-guides';
import { CALCULATOR_PAGES } from '@/content/calculator-pages';

describe('calculator extras coverage', () => {
  it('has an entry for every slug that is exported as a page', () => {
    const missing = STRATEGY_SLUGS.filter((slug) => !getCalculatorExtra(slug));
    expect(missing).toEqual([]);
  });

  it('has no entry for a slug that is not a page', () => {
    const orphans = Object.keys(CALCULATOR_EXTRAS).filter(
      (slug) => !(STRATEGY_SLUGS as readonly string[]).includes(slug),
    );
    expect(orphans).toEqual([]);
  });

  it('declares its own slug on every entry', () => {
    for (const [key, extra] of Object.entries(CALCULATOR_EXTRAS)) {
      expect(extra.slug).toBe(key);
    }
  });

  it('gives every strategy a leg table, a cap pair, and either a grid, a ratio table, a sensitivity table, or the no-closed-form flag', () => {
    for (const extra of Object.values(CALCULATOR_EXTRAS)) {
      expect(extra.legs.length, `${extra.slug} legs`).toBeGreaterThan(0);
      expect(extra.caps.profitCap.length, `${extra.slug} profitCap`).toBeGreaterThan(0);
      expect(extra.caps.lossCap.length, `${extra.slug} lossCap`).toBeGreaterThan(0);
      const hasNumericContent =
        Boolean(extra.grid) ||
        Boolean(extra.ratioTable) ||
        Boolean(extra.sensitivityTable) ||
        Boolean(extra.noClosedForm);
      expect(hasNumericContent, `${extra.slug} has no numeric block`).toBe(true);
    }
  });
});

/*
 * The arithmetic gate. Every pair below is a checkpoint already stated in
 * `strategy-guides.ts`'s own worked example (a max profit, a max loss, or a
 * named "profit/loss if X" row) — this recomputes nothing new, it proves the
 * grid in `calculator-extras.ts` reproduces the SAME figure at the SAME price,
 * so a transcribed strike or premium that drifts from the guide is caught
 * rather than shipped as a quietly wrong number on the page.
 *
 * [slug, grid x-value (as the row's `x` string), expected `pnl` string]
 */
const CHECKPOINTS: Array<[string, string, string]> = [
  ['long-call', '545', '-$725.00'],
  ['long-put', '530', '-$940.00'],
  ['call-spread', '560', '-$725.00'],
  ['call-spread', '630', '$1275.00'],
  ['put-spread', '250', '-$510.00'],
  ['put-spread', '200', '$990.00'],
  ['bull-put-spread', '595', '$280.00'],
  ['bull-put-spread', '545', '-$720.00'],
  ['bear-call-spread', '170', '$235.00'],
  ['bear-call-spread', '220', '-$765.00'],
  ['straddle', '340', '-$3470.00'],
  ['strangle', '500', '-$1350.00'],
  ['iron-condor', '520', '-$710.00'],
  ['iron-condor', '580', '$290.00'],
  ['iron-butterfly', '580', '$920.00'],
  ['iron-butterfly', '550', '-$1080.00'],
  ['butterfly', '580', '$790.00'],
  ['butterfly', '560', '-$210.00'],
  ['condor', '580', '$1060.00'],
  ['condor', '545', '-$440.00'],
  ['jade-lizard', '170', '$425.00'],
  ['jade-lizard', '210', '-$75.00'],
  ['cash-secured-put', '500', '-$5440.00'],
  ['protective-put', '130', '-$1180.00'],
  ['protective-put', '220', '$4320.00'],
  ['collar', '605', '$4485.00'],
  ['collar', '520', '-$15.00'],
  ['risk-reversal', '500', '-$4980.00'],
  ['risk-reversal', '580', '$20.00'],
  ['futures-outright', '5900', '$5000.00'],
  ['futures-outright', '5700', '-$5000.00'],
  ['futures-spread', '-30', '$750.00'],
  ['futures-spread', '-60', '-$750.00'],
  ['futures-calendar-spread', '-0.7', '$1000.00'],
  ['futures-calendar-spread', '-2.7', '-$1000.00'],
  ['covered-futures-call', '5900', '$7100.00'],
  ['covered-futures-call', '5600', '-$7900.00'],
];

describe('payoff grid arithmetic matches the guide', () => {
  it.each(CHECKPOINTS)('%s at %s reproduces %s', (slug, x, expected) => {
    const grid = CALCULATOR_EXTRAS[slug].grid;
    expect(grid, `${slug} has no grid`).toBeDefined();
    const row = grid!.rows.find((r) => r.x === x);
    expect(row, `${slug} grid has no row at ${x}`).toBeDefined();
    expect(row!.pnl, `${slug} at ${x}`).toBe(expected);
  });

  it('covered-call is capped: pnl at the strike equals pnl further above it', () => {
    const grid = CALCULATOR_EXTRAS['covered-call'].grid!;
    const atStrike = grid.rows.find((r) => r.x === '240')!.pnl;
    const aboveStrike = grid.rows.find((r) => r.x === '280')!.pnl;
    expect(atStrike).toBe('$2410.00');
    expect(aboveStrike).toBe(atStrike);
  });

  it('the two no-closed-form strategies carry no fabricated grid', () => {
    expect(CALCULATOR_EXTRAS['calendar-spread'].grid).toBeUndefined();
    expect(CALCULATOR_EXTRAS['diagonal-spread'].grid).toBeUndefined();
    expect(CALCULATOR_EXTRAS['calendar-spread'].noClosedForm).toBe(true);
    expect(CALCULATOR_EXTRAS['diagonal-spread'].noClosedForm).toBe(true);
  });
});

describe('mechanics and note prose are substantive and distinct', () => {
  const extras = Object.values(CALCULATOR_EXTRAS);
  const PROSE_FIELDS = ['mechanics', 'note'] as const;

  it.each(PROSE_FIELDS)('writes a %s paragraph of 60 to 150 words on every page', (field) => {
    for (const extra of extras) {
      const words = extra[field].trim().split(/\s+/).length;
      expect(words, `${extra.slug} ${field} words`).toBeGreaterThanOrEqual(60);
      expect(words, `${extra.slug} ${field} words`).toBeLessThanOrEqual(150);
    }
  });

  it.each(PROSE_FIELDS)('writes a distinct %s for every strategy', (field) => {
    const seen = new Map<string, string>();
    for (const extra of extras) {
      const previous = seen.get(extra[field]);
      expect(previous, `${extra.slug} and ${previous} share identical ${field}`).toBeUndefined();
      seen.set(extra[field], extra.slug);
    }
  });

  it('writes a distinct note from its own mechanics, on every page', () => {
    for (const extra of extras) {
      expect(extra.note, `${extra.slug} note repeats its mechanics`).not.toBe(extra.mechanics);
    }
  });

  it('writes a one-sentence mistake of 20 to 50 words, distinct across strategies', () => {
    const seen = new Map<string, string>();
    for (const extra of extras) {
      const words = extra.mistake.trim().split(/\s+/).length;
      expect(words, `${extra.slug} mistake words`).toBeGreaterThanOrEqual(20);
      expect(words, `${extra.slug} mistake words`).toBeLessThanOrEqual(50);
      const previous = seen.get(extra.mistake);
      expect(previous, `${extra.slug} and ${previous} share an identical mistake`).toBeUndefined();
      seen.set(extra.mistake, extra.slug);
    }
  });

  /*
   * Cross-family, both directions: this content must not restate the lede
   * (`calculator-pages.ts`) or the guide's own prose (`strategy-guides.ts`)
   * for the same strategy. Sentence-level, matching the standard
   * `check-export.mjs` and `calculator-pages.test.ts` already apply to the
   * lede-vs-guide pair.
   */
  const ALL_PROSE_FIELDS = ['mechanics', 'note', 'mistake'] as const;

  it.each(ALL_PROSE_FIELDS)('%s shares no sentence with its own lede or its own guide', (field) => {
    for (const extra of extras) {
      const lede = CALCULATOR_PAGES[extra.slug]?.lede ?? '';
      const guide = STRATEGY_GUIDES[extra.slug];
      const guideProse = guide
        ? [guide.lede, guide.whenToUse, ...guide.risks].join(' ')
        : '';
      for (const sentence of extra[field].split(/(?<=\.)\s+/)) {
        if (sentence.length < 40) continue;
        expect(lede.includes(sentence), `${extra.slug} ${field} reuses its lede`).toBe(false);
        expect(
          guideProse.includes(sentence),
          `${extra.slug} ${field} reuses a guide sentence`,
        ).toBe(false);
      }
    }
  });

  it('shares no sentence across its own mechanics, note and mistake', () => {
    for (const extra of extras) {
      const pairs: Array<[(typeof ALL_PROSE_FIELDS)[number], (typeof ALL_PROSE_FIELDS)[number]]> = [
        ['note', 'mechanics'],
        ['mistake', 'mechanics'],
        ['mistake', 'note'],
      ];
      for (const [a, b] of pairs) {
        for (const sentence of extra[a].split(/(?<=\.)\s+/)) {
          if (sentence.length < 40) continue;
          expect(
            extra[b].includes(sentence),
            `${extra.slug} ${a} reuses a ${b} sentence`,
          ).toBe(false);
        }
      }
    }
  });
});

describe('the comparison data is symmetric with STRATEGY_GUIDES', () => {
  it('every related slug named in a guide has extras of its own', () => {
    for (const guide of Object.values(STRATEGY_GUIDES)) {
      for (const relSlug of guide.related ?? []) {
        expect(getCalculatorExtra(relSlug), `${guide.slug} -> ${relSlug}`).toBeDefined();
      }
    }
  });
});
