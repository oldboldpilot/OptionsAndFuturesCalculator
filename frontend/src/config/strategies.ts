/**
 * The strategy slugs that exist as pages.
 *
 * Shared rather than declared inside the route file, because two things need
 * this list and they must not disagree: `generateStaticParams`, which decides
 * which pages get exported, and the sitemap, which tells crawlers those pages
 * exist. When the sitemap was hand-written it listed pages on another domain
 * entirely -- the failure mode of a second, independently maintained copy.
 */
export const STRATEGY_SLUGS = [
  'long-call',
  'long-put',
  'call-spread',
  'put-spread',
  'bull-put-spread',
  'bear-call-spread',
  'straddle',
  'strangle',
  'iron-condor',
  'iron-butterfly',
  'butterfly',
  'condor',
  'collar',
  'covered-call',
  'cash-secured-put',
  'protective-put',
  'jade-lizard',
  'calendar-spread',
  'diagonal-spread',
  'risk-reversal',
  'futures-spread',
  'futures-outright',
  'futures-calendar-spread',
  'futures-intercommodity-spread',
  'covered-futures-call',
  'futures-basis-arbitrage',
] as const;

export type StrategySlug = (typeof STRATEGY_SLUGS)[number];

/**
 * The picker strategy each calculator page opens on.
 *
 * A page named "Long Call Calculator" used to render the same generic workspace
 * as all the others, with nothing selected. The slug vocabulary (the URLs, the
 * guides, the sitemap) and the picker's ids (`long_call`, `bull_call_spread`)
 * are two different vocabularies, so the join is written out. It is checked from
 * both directions in `strategy-pages.test.ts`: a slug with no entry, or an entry
 * the picker does not offer, fails the build.
 */
export const PICKER_STRATEGY_FOR_SLUG: Readonly<Record<StrategySlug, string>> = {
  'long-call': 'long_call',
  'long-put': 'long_put',
  'call-spread': 'bull_call_spread',
  'put-spread': 'bear_put_spread',
  'bull-put-spread': 'bull_put_spread',
  'bear-call-spread': 'bear_call_spread',
  straddle: 'long_straddle',
  strangle: 'long_strangle',
  'iron-condor': 'iron_condor',
  'iron-butterfly': 'iron_butterfly',
  butterfly: 'call_butterfly',
  condor: 'condor',
  collar: 'collar',
  'covered-call': 'covered_call',
  'cash-secured-put': 'cash_secured_put',
  'protective-put': 'protective_put',
  'jade-lizard': 'jade_lizard',
  'calendar-spread': 'calendar_spread',
  'diagonal-spread': 'diagonal_spread',
  'risk-reversal': 'risk_reversal',
  'futures-spread': 'futures_calendar',
  'futures-outright': 'futures_long',
  'futures-calendar-spread': 'futures_calendar',
  'futures-intercommodity-spread': 'spark_spread',
  'covered-futures-call': 'covered_futures_call',
  'futures-basis-arbitrage': 'cash_and_carry',
};

/** Pages whose structure only exists on a futures symbol. */
export const FUTURES_SLUGS: ReadonlySet<StrategySlug> = new Set<StrategySlug>([
  'futures-spread',
  'futures-outright',
  'futures-calendar-spread',
  'futures-intercommodity-spread',
  'covered-futures-call',
  'futures-basis-arbitrage',
]);
