/**
 * Futures roots this calculator knows about, and what one contract is worth.
 *
 * Reference data about WHAT AN INSTRUMENT IS, not a market observation, so a
 * hardcoded table is legitimate -- the same standing as the symbol
 * classification in the store. Only the multiplier is load-bearing: it is the
 * dollars one point of price is worth, and without it a futures position's
 * profit and loss comes out in INDEX POINTS (a leg was sent with multiplier 1),
 * so a long ES contract that gains 10 points read +$10 instead of +$500.
 *
 * Every figure is the exchange's published contract specification. A root with
 * no entry is not guessed at: `futuresMultiplier` returns undefined and the
 * caller refuses.
 */
export interface FuturesRoot {
  name: string;
  /** Dollars per one point of price, for one contract. */
  multiplier: number;
}

export const FUTURES_ROOTS: Readonly<Record<string, FuturesRoot>> = {
  ES: { name: 'E-mini S&P 500', multiplier: 50 },
  NQ: { name: 'E-mini Nasdaq-100', multiplier: 20 },
  RTY: { name: 'E-mini Russell 2000', multiplier: 50 },
  YM: { name: 'E-mini Dow', multiplier: 5 },
  CL: { name: 'WTI crude oil', multiplier: 1000 },
  NG: { name: 'Henry Hub natural gas', multiplier: 10000 },
  GC: { name: 'Gold', multiplier: 100 },
  SI: { name: 'Silver', multiplier: 5000 },
  ZB: { name: '30-year Treasury bond', multiplier: 1000 },
  ZN: { name: '10-year Treasury note', multiplier: 1000 },
};

export function futuresMultiplier(root: string): number | undefined {
  return FUTURES_ROOTS[root]?.multiplier;
}
