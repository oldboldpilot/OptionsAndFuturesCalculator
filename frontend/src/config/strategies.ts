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
