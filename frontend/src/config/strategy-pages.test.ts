/**
 * Every `/calculator/<slug>` page opens on the strategy it is named for (OFC-027).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * "Long Call Calculator" rendered the same generic workspace as every other
 * page -- zero legs, nothing selected -- so the page named for a structure did
 * not know which structure it was. The mapping from a page's slug to an entry in
 * the strategy picker is hand-kept data, and a hand-kept mapping rots in exactly
 * one way: a slug is added, or a picker entry renamed, and the page quietly
 * opens on nothing again. So it is checked from both sides.
 */
import { describe, it, expect } from 'vitest';
import { STRATEGY_SLUGS, PICKER_STRATEGY_FOR_SLUG, FUTURES_SLUGS } from './strategies';
import { SELECTABLE_STRATEGY_IDS } from '../components/StrategySelector';

describe('PICKER_STRATEGY_FOR_SLUG', () => {
  it('names a picker strategy for every calculator page', () => {
    const missing = STRATEGY_SLUGS.filter((s) => !PICKER_STRATEGY_FOR_SLUG[s]);
    expect(missing).toEqual([]);
  });

  it('only names strategies the picker actually offers', () => {
    const offered = new Set(SELECTABLE_STRATEGY_IDS);
    const dangling = Object.entries(PICKER_STRATEGY_FOR_SLUG)
      .filter(([, id]) => !offered.has(id))
      .map(([slug, id]) => `${slug} -> ${id}`);
    expect(dangling).toEqual([]);
  });

  it('has no entry for a page that does not exist', () => {
    const known = new Set<string>(STRATEGY_SLUGS);
    expect(Object.keys(PICKER_STRATEGY_FOR_SLUG).filter((s) => !known.has(s))).toEqual([]);
  });

  it('puts the one-leg pages on their one leg', () => {
    expect(PICKER_STRATEGY_FOR_SLUG['long-call']).toBe('long_call');
    expect(PICKER_STRATEGY_FOR_SLUG['long-put']).toBe('long_put');
    expect(PICKER_STRATEGY_FOR_SLUG['cash-secured-put']).toBe('cash_secured_put');
  });
});

describe('FUTURES_SLUGS', () => {
  it('marks exactly the pages that need a futures symbol', () => {
    expect([...FUTURES_SLUGS].sort()).toEqual(
      STRATEGY_SLUGS.filter((s) => s.startsWith('futures-') || s === 'covered-futures-call').sort(),
    );
  });

  it('only contains pages that exist', () => {
    for (const s of FUTURES_SLUGS) expect((STRATEGY_SLUGS as readonly string[]).includes(s)).toBe(true);
  });
});
