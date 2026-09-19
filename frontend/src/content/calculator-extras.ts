/**
 * Second wave of per-strategy content for `/calculator/<slug>`, added below
 * the lede in `calculator-pages.ts`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Why this exists on top of the lede: measured against the LIVE site on
 * 2026-09-19, with the 80-120 word ledes from `calculator-pages.ts` already
 * shipped, the 26 `/calculator/<slug>` pages still carried a pairwise median
 * 6-gram Jaccard similarity of 0.768 — a shared workspace shell of roughly 830
 * words dwarfs a single paragraph of unique prose. Bringing that down needs
 * genuinely more unique, useful material per page, not a longer version of the
 * same template.
 *
 * Four kinds of content live here, and each is arithmetic or structured data
 * rather than a second lede:
 *
 *   MECHANICS. One paragraph per strategy about ORDER CONSTRUCTION and
 *   EXECUTION — order-ticket shape, margin treatment, assignment mechanics,
 *   liquidity — deliberately a different angle from the lede (payoff shape and
 *   psychology) and from the guide's `whenToUse`/`risks` (view and failure
 *   mode). No sentence here should read like either.
 *
 *   LEGS. The concrete order ticket for the strategy's own worked example —
 *   actual numbers, not the guide's symbolic K/K₁/K₂ construction list.
 *
 *   CAPS. Short (non-sentence) phrases naming what bounds profit and loss.
 *
 *   GRID / RATIO / SENSITIVITY tables. Arithmetic on the SAME numbers already
 *   in the strategy's own worked example in `strategy-guides.ts` — nothing
 *   invented. `calculator-extras.test.ts` recomputes the closed-form
 *   maxProfit/maxLoss/breakeven independently from these params and checks
 *   they agree with the guide's own prose, so a transcription error here is
 *   caught rather than silently shipped as a wrong number on the site.
 *
 *   Two strategies — `calendar-spread` and `diagonal-spread` — have no
 *   closed-form payoff (the guide says so explicitly: the back leg is still
 *   alive when the front expires and has to be valued by a model). No grid is
 *   built for them; inventing one would misstate what the guide itself says.
 */

/* ------------------------------- Payoff math ------------------------------ */

const callV = (S: number, K: number): number => Math.max(S - K, 0);
const putV = (S: number, K: number): number => Math.max(K - S, 0);
const clamp = (v: number, lo: number, hi: number): number => Math.min(Math.max(v, lo), hi);

const money = (n: number): string => {
  const rounded = Math.round(n * 100) / 100;
  return rounded < 0 ? `-$${Math.abs(rounded).toFixed(2)}` : `$${rounded.toFixed(2)}`;
};

/** Per-share P&L, expressed as one equity options contract (×100). */
const perContract = (perShare: number): string => money(perShare * 100);

export interface LegRow {
  action: string;
  instrument: string;
  strike: string;
}

export interface GlanceCaps {
  profitCap: string;
  lossCap: string;
}

export interface GridRow {
  x: string;
  pnl: string;
}

export interface PriceGrid {
  axisLabel: string;
  rows: GridRow[];
}

export interface RatioRow {
  role: string;
  instrument: string;
  ratio: string;
}

export interface SensitivityRow {
  label: string;
  value: string;
}

export interface CalculatorExtra {
  slug: string;
  /** ~70-100 words on order construction and execution, distinct per strategy. */
  mechanics: string;
  /** ~70-100 words on sizing, margin, tax and rolling — a third distinct angle. */
  note: string;
  /** One sentence naming the specific execution or behavioural failure. */
  mistake: string;
  legs: LegRow[];
  caps: GlanceCaps;
  grid?: PriceGrid;
  ratioTable?: RatioRow[];
  sensitivityTable?: { axisLabel: string; rows: SensitivityRow[] };
  noClosedForm?: boolean;
}

/* Grid builders, one per payoff shape. Every parameter below is the same
 * number already stated in that strategy's `example` in `strategy-guides.ts`. */

const grid = (axisLabel: string, xs: number[], pnl: (x: number) => number): PriceGrid => ({
  axisLabel,
  rows: xs.map((x) => ({ x: String(x), pnl: perContract(pnl(x)) })),
});

const futuresGrid = (axisLabel: string, xs: number[], pnl: (x: number) => number): PriceGrid => ({
  axisLabel,
  rows: xs.map((x) => ({ x: String(x), pnl: money(pnl(x)) })),
});

export const CALCULATOR_EXTRAS: Record<string, CalculatorExtra> = {
  'long-call': {
    slug: 'long-call',
    mechanics:
      "This is a single-leg ticket: one call, one strike, one expiry — the simplest order this tool builds. Strikes trade in fixed increments (often $1 or $2.50 near the money on a liquid name, wider further out), so the strike a model prefers is not always the strike you can fill. Liquidity concentrates at round strikes and monthly expiries; a strike between them can carry a wider bid/ask that eats into the edge a backtest assumed. Check open interest before sizing a position larger than the visible depth can absorb.",
    note:
      "Position size against the premium at risk, not against the notional the call controls — a $725 call on 100 shares of a $580 stock is often sized as though it were a $58,000 position, which overstates the loss it can actually produce. Many traders scale into a directional call across two or three tranches rather than one, buying more if the thesis is confirmed rather than committing the full size on day one. Wash-sale rules can apply if a losing call is closed and a similar one reopened within 30 days — relevant mainly to repeated short-dated trades on the same name.",
    mistake:
      "The common mistake is buying a strike so far out of the money that even a correct call on direction never clears it before the premium decays to nothing — the breakeven, not the current price, is the number that matters.",
    legs: [{ action: 'Buy', instrument: 'Call', strike: '585' }],
    caps: { profitCap: 'Uncapped above the strike', lossCap: 'Limited to the premium paid' },
    grid: grid('SPY at expiry', [545, 565, 585, 605, 625, 645], (S) => callV(S, 585) - 7.25),
  },

  'long-put': {
    slug: 'long-put',
    mechanics:
      'Also one leg, but the order ticket matters more here than it looks: a put on a hard-to-borrow name can carry extra premium from the cost of the corresponding short. Strike selection is a trade-off against skew — moving one increment further out of the money buys meaningfully more convexity on an index than on a single stock, because index skew is steeper. For a hedge sized against a specific share count, round the contract count down rather than up; over-hedging turns protection into a second, unwanted short position.',
    note:
      "Sized against the premium paid when speculative, and against the shares actually held when it is a hedge — conflating the two is the most common sizing mistake, since a hedge should track share count, not conviction. Brokers generally require only a standard options-approval level for a long put, well below what a naked short put or a spread needs, which is part of why it is often the first multi-leg-adjacent structure a new account is approved for. A put purchased and closed within a year is short-term for tax purposes regardless of how long the underlying shares have been held.",
    mistake:
      "The common mistake is sizing the put by how bearish the view feels rather than by the shares actually being hedged, which leaves a hedge that either under-covers a real position or speculates well beyond it.",
    legs: [{ action: 'Buy', instrument: 'Put', strike: '490' }],
    caps: {
      profitCap: 'Bounded: strike minus premium, if the stock goes to zero',
      lossCap: 'Limited to the premium paid',
    },
    grid: grid('QQQ at expiry', [430, 450, 470, 490, 510, 530], (S) => putV(S, 490) - 9.4),
  },

  'call-spread': {
    slug: 'call-spread',
    mechanics:
      "Two legs on one ticket, entered as a single 'vertical' order rather than as two separate trades — most platforms price and margin it that way, which avoids being filled on one leg and not the other in a moving market. The strike gap sets both the cost and the ceiling: a $5-wide spread behaves very differently from a $20-wide one at the same debit-to-width ratio. Compare the vertical's mid-price against the sum of the two legs' own mids; a wide bid/ask on the short leg alone can make the spread look cheaper than it fills.",
    note:
      "The width is also a margin decision on some accounts: a cash-secured account needs the full debit and nothing more, since the risk is capped at what was paid, which is one reason this structure is often approved at a lower account tier than a naked short call would need. Scaling into the position by adding contracts as a thesis develops works less cleanly here than with a single option, because each new tranche has its own two fills and its own bid/ask cost. Held to expiry, both legs typically settle automatically if in the money; check the broker's auto-exercise threshold if the spread finishes only a few cents in the money.",
    mistake:
      "The common mistake is setting the short strike at a round number that looks psychologically significant rather than at the level the underlying is actually likely to reach, capping the trade below its own thesis.",
    legs: [
      { action: 'Buy', instrument: 'Call', strike: '580' },
      { action: 'Sell', instrument: 'Call', strike: '600' },
    ],
    caps: {
      profitCap: 'Capped: spread width minus the debit',
      lossCap: 'Limited to the net debit paid',
    },
    grid: grid('SPY at expiry', [560, 575, 590, 600, 615, 630], (S) =>
      clamp(S - 580, 0, 20) - 7.25,
    ),
  },

  'put-spread': {
    slug: 'put-spread',
    mechanics:
      "Priced and margined as a single vertical, same as the call version, which matters because legging in manually exposes you to the underlying moving between the two fills. The short leg's strike is the real lever: pushing it lower raises the credit received against the long put and narrows the width, a cost decision as much as a directional one. Because downside strikes carry more implied volatility than equivalent upside ones, the short leg here typically funds more of the long put than a same-width call spread funds its long call.",
    note:
      "Because the short leg funds part of the long one, this is usually approved at the same account tier as the bull call spread rather than at the tier a naked short put needs — the defined risk is what qualifies it. Tax treatment follows the usual short-term/long-term line at one year regardless of the spread's own life, and the two legs are treated as separate lots for that purpose even though they were opened and will likely close together. A partial fill — one leg executing before the other — briefly leaves a naked position; many platforms reject partial fills on a combined order for exactly this reason.",
    mistake:
      "The common mistake is setting the short strike too close to the long one to save on debit, which shrinks the maximum profit far more than it shrinks the risk of the trade being wrong.",
    legs: [
      { action: 'Buy', instrument: 'Put', strike: '230' },
      { action: 'Sell', instrument: 'Put', strike: '215' },
    ],
    caps: {
      profitCap: 'Capped: spread width minus the debit',
      lossCap: 'Limited to the net debit paid',
    },
    grid: grid('AAPL at expiry', [200, 210, 220, 230, 240, 250], (S) =>
      clamp(230 - S, 0, 15) - 5.1,
    ),
  },

  'bull-put-spread': {
    slug: 'bull-put-spread',
    mechanics:
      "The short leg is the one whose bid/ask to check first, since it is usually the less liquid of the two strikes and the one a fill price actually depends on. Brokers margin this as the width minus the credit, held as buying power rather than posted in cash, so the return quoted on the credit alone overstates the return on capital actually tied up. Closing early — buying back both legs before expiry — avoids the week where gamma risk is highest, at the cost of some of the remaining credit.",
    note:
      "Size this by the maximum loss (width minus credit), never by the credit alone — a $280 credit against a $720 maximum loss is a very different commitment of capital than the $280 figure suggests on its own. Most platforms require the full maximum loss as buying power even in a margin account, since the position's risk is genuinely capped rather than merely usually small. A common practice is capping any single credit spread at a small fraction of account equity, precisely because the loss-to-credit ratio here is structurally lopsided.",
    mistake:
      "The common mistake is chasing a larger credit by narrowing the strike gap, which raises the probability of a total loss on the position by more than the extra credit compensates for.",
    legs: [
      { action: 'Sell', instrument: 'Put', strike: '570' },
      { action: 'Buy', instrument: 'Put', strike: '560' },
    ],
    caps: { profitCap: 'Capped: the net credit received', lossCap: 'Capped: spread width minus the credit' },
    grid: grid('SPY at expiry', [545, 555, 565, 575, 585, 595], (S) =>
      2.8 - clamp(570 - S, 0, 10),
    ),
  },

  'bear-call-spread': {
    slug: 'bear-call-spread',
    mechanics:
      'Structured and margined the same way as the put-side credit spread, with buying power set by the width minus the credit. The short call is the leg most likely to move against you overnight on single-name news, so position size here should account for a gap risk the payoff diagram itself does not show. A calendar check on the short strike\'s next earnings date is worth doing before entry; this structure is not usually opened deliberately through an event.',
    note:
      "The margin held is the same width-minus-credit figure as the put-side version, and it is worth comparing the two side by side on the same underlying before choosing — equity index skew usually makes the put spread's credit larger for the same width, so the two are not mirror images in practice even though the payoff shapes are. Because the short call is the leg most exposed to a surprise announcement, some traders avoid opening new positions here in the days immediately before a name's scheduled earnings release. Closing at a fixed fraction of the credit, rather than holding to expiry, is the more common way this is managed.",
    mistake:
      "The common mistake is opening this into a name with a scheduled catalyst nearby, treating a defined-risk structure as immune to a gap that can consume the entire width overnight.",
    legs: [
      { action: 'Sell', instrument: 'Call', strike: '185' },
      { action: 'Buy', instrument: 'Call', strike: '195' },
    ],
    caps: { profitCap: 'Capped: the net credit received', lossCap: 'Capped: spread width minus the credit' },
    grid: grid('NVDA at expiry', [170, 180, 190, 200, 210, 220], (S) =>
      2.35 - clamp(S - 185, 0, 10),
    ),
  },

  straddle: {
    slug: 'straddle',
    mechanics:
      "Both legs share one strike and one expiry, so the ticket is simpler than it looks — the cost comes from paying full premium on two at-the-money options, not from any complexity in the order. Because the position prices almost entirely off implied volatility rather than direction, compare the at-the-money implied volatility here against where it has recently traded before paying up into an already-elevated level. Exiting before an expected volatility drop, rather than holding through it, is the more common way this is actually traded.",
    note:
      "Size by the combined debit, and treat it as the full amount at risk rather than as two half-sized positions, since only one leg can ever pay and the other is a near-certain loss by construction. Implied volatility on the specific expiry chosen matters more than the underlying's general volatility level — a straddle priced into an earnings week embeds an event premium a straddle on an ordinary week does not, and the two are not comparable at face value. Many traders close before the event resolves if implied volatility has run up sharply, taking the vega gain rather than holding through the outcome.",
    mistake:
      "The common mistake is buying it into a well-telegraphed event where implied volatility is already pricing the expected move, so being right about the event still loses if the move is merely average.",
    legs: [
      { action: 'Buy', instrument: 'Call', strike: '340' },
      { action: 'Buy', instrument: 'Put', strike: '340' },
    ],
    caps: {
      profitCap: 'Uncapped on a large move either way',
      lossCap: 'Limited to the combined debit',
    },
    grid: grid('TSLA at expiry', [280, 310, 340, 370, 400, 430], (S) =>
      Math.abs(S - 340) - 34.7,
    ),
  },

  strangle: {
    slug: 'strangle',
    mechanics:
      'Two legs, two different strikes, same expiry — priced as a single multi-leg order the same way a vertical is. Because both legs are out of the money, this is cheaper to enter than a straddle at the same expiry but more sensitive to exactly where the strikes sit relative to the current price; moving either strike one increment closer to the money raises the cost more than it raises the odds of finishing in it. Match the strikes by delta rather than by a round dollar distance from spot to keep the position close to direction-neutral.',
    note:
      "Cheaper than the straddle at the same expiry, which lets an account hold more contracts for the same capital — but the dead zone between the strikes is wide, and sizing up to compensate for the lower per-unit cost does not change the odds of finishing inside it. Matching the two legs by delta rather than by a fixed dollar distance from spot keeps the position close to direction-neutral on a skewed underlying, which a naive equal-distance choice will not. As with the straddle, most of the premium is lost to a quiet week rather than to being wrong about direction.",
    mistake:
      "The common mistake is choosing the two strikes by a round dollar distance from spot rather than by delta, which quietly biases the position toward one direction on a skewed underlying.",
    legs: [
      { action: 'Buy', instrument: 'Call', strike: '520' },
      { action: 'Buy', instrument: 'Put', strike: '480' },
    ],
    caps: {
      profitCap: 'Uncapped on a large move either way',
      lossCap: 'Limited to the combined debit',
    },
    grid: grid('QQQ at expiry', [430, 470, 500, 520, 560, 600], (S) =>
      callV(S, 520) + putV(S, 480) - 13.5,
    ),
  },

  'iron-condor': {
    slug: 'iron-condor',
    mechanics:
      "Four legs, two verticals, usually filled as one combined order — most platforms treat 'iron condor' as its own order type rather than four separate legs, which is what makes the execution practical. The two short strikes are the entire decision; the long wings exist only to define the risk and are usually placed at a fixed width rather than chosen independently. Because it is a net-credit, defined-risk position, margin is set by the wider wing minus the total credit, not by the sum of both wings.",
    note:
      "Margin is set by the wider wing minus the total credit, so widening one wing without widening the other raises the buying power required without raising the credit collected — check both together, not just the credit, when comparing two condors on the same underlying. Many traders size a condor so the credit is a fixed fraction of the maximum loss, commonly a third or better, rather than sizing purely by contract count. Rolling the untested side to collect additional credit after the market has moved is a common adjustment; it changes both breakevens and should be planned before the position is opened, not improvised after.",
    mistake:
      "The common mistake is widening the short strikes for a higher win rate without checking that the resulting credit still compensates for the wider wing's now-larger maximum loss.",
    legs: [
      { action: 'Buy', instrument: 'Put', strike: '545' },
      { action: 'Sell', instrument: 'Put', strike: '555' },
      { action: 'Sell', instrument: 'Call', strike: '605' },
      { action: 'Buy', instrument: 'Call', strike: '615' },
    ],
    caps: {
      profitCap: 'Capped: the total credit received',
      lossCap: 'Capped: wider wing minus the credit',
    },
    grid: grid('SPY at expiry', [520, 545, 580, 605, 615, 640], (S) =>
      2.9 - clamp(555 - S, 0, 10) - clamp(S - 605, 0, 10),
    ),
  },

  'iron-butterfly': {
    slug: 'iron-butterfly',
    mechanics:
      'The short call and short put share one strike, so the order is really three distinct strikes rather than four — fill quality on that single at-the-money strike matters more here than in a condor, since both short legs depend on it. Widening the wings raises the maximum loss and lowers it as a fraction of the credit, the one dial available once the centre strike is fixed by where the underlying happens to be trading. Assignment risk on the short straddle at the centre is worth planning for before expiry week, not during it.',
    note:
      "Because the credit is large relative to the width, the margin required is smaller as a fraction of the position's notional risk than a comparable iron condor's — but the range that keeps it is also much narrower, so the higher credit-to-margin ratio is compensation for a lower probability of finishing inside the range, not a free improvement. Pin risk at the shared centre strike is worth planning an exit around before expiry week; many traders close both short legs a day or two early specifically to avoid an ambiguous assignment at the close.",
    mistake:
      "The common mistake is holding through expiry week hoping for the exact pin, when closing a day or two early for most of the credit avoids the assignment ambiguity at the shared centre strike.",
    legs: [
      { action: 'Sell', instrument: 'Call + Put', strike: '580' },
      { action: 'Buy', instrument: 'Put', strike: '560' },
      { action: 'Buy', instrument: 'Call', strike: '600' },
    ],
    caps: {
      profitCap: 'Capped: the credit, only exactly at the centre',
      lossCap: 'Capped: wing width minus the credit',
    },
    grid: grid('SPY at expiry', [550, 565, 580, 595, 610, 625], (S) =>
      9.2 - clamp(580 - S, 0, 20) - clamp(S - 580, 0, 20),
    ),
  },

  butterfly: {
    slug: 'butterfly',
    mechanics:
      "Three strikes and four contracts on one ticket — two short calls at the middle strike against one long call on each side. Equal spacing between the strikes is what keeps the payoff symmetric; an uneven spread is a different, skewed structure even though it still has three legs. Because the debit is small relative to the width, the round-trip bid/ask on four contracts is a proportionally large cost — check the combined order's mid-price against the four individual mids before assuming the displayed debit is what will actually fill.",
    note:
      "The debit paid is the entire risk, so sizing is simple — the harder part is execution, since three strikes and four contracts routinely cost more in combined bid/ask than the theoretical debit implies on a screen showing mid-prices. Many traders wait for the debit to reach a specific level relative to the width, commonly under a fifth of it, before entering, since the position does very little until the final two weeks regardless of when it was opened. Because two contracts are sold at the same strike, some brokers report this as three legs rather than four on a trade confirmation — the position is still four contracts.",
    mistake:
      "The common mistake is entering with a wide market on the middle strike's two contracts, paying away most of the edge in execution cost before the position has even had a chance to work.",
    legs: [
      { action: 'Buy', instrument: 'Call', strike: '570' },
      { action: 'Sell', instrument: 'Call ×2', strike: '580' },
      { action: 'Buy', instrument: 'Call', strike: '590' },
    ],
    caps: {
      profitCap: 'Capped: only exactly at the middle strike',
      lossCap: 'Limited to the net debit paid',
    },
    grid: grid('SPY at expiry', [560, 570, 580, 590, 600, 610], (S) =>
      callV(S, 570) - 2 * callV(S, 580) + callV(S, 590) - 2.1,
    ),
  },

  condor: {
    slug: 'condor',
    mechanics:
      'Four distinct strikes on one ticket, unlike the butterfly\'s repeated middle strike — the structure to reach for when a single pinned price is too precise a bet to make. The gap between the two short strikes sets the width of the plateau where the maximum profit is held; widening it trades away some of the maximum for a larger window of prices that pay it. Compare this against pricing the equivalent iron condor at the same four strikes before choosing — the payoff is nearly identical and the choice usually comes down to which side has the tighter market.',
    note:
      "The wider plateau between the two short strikes is the trade-off against the butterfly's larger peak; size and strike choice should follow how confident the view is about a range rather than a single price, since paying more debit for a narrower plateau buys precision that a range view does not need. As with the butterfly, most of the position's value arrives in the final weeks before expiry, so an early exit at a small loss is common and not a sign the thesis was wrong, only that it has not yet had time to play out.",
    mistake:
      "The common mistake is treating the four-strike debit version and the equivalent iron condor as interchangeable without comparing which side's market is actually tighter on the day.",
    legs: [
      { action: 'Buy', instrument: 'Call', strike: '560' },
      { action: 'Sell', instrument: 'Call', strike: '575' },
      { action: 'Sell', instrument: 'Call', strike: '585' },
      { action: 'Buy', instrument: 'Call', strike: '600' },
    ],
    caps: {
      profitCap: 'Capped: held across the inner plateau',
      lossCap: 'Limited to the net debit paid',
    },
    grid: grid('SPY at expiry', [545, 565, 580, 590, 605, 620], (S) =>
      callV(S, 560) - callV(S, 575) - callV(S, 585) + callV(S, 600) - 4.4,
    ),
  },

  'jade-lizard': {
    slug: 'jade-lizard',
    mechanics:
      'Three legs on one ticket: a short put and a call spread, usually entered as two separate combined orders rather than one three-leg ticket, since few platforms bundle a put with a call spread automatically. The credit rule — total credit at least the call spread\'s width — has to be checked against the actual fill prices, not the mid-quotes used to plan the trade, because a wide market on either leg can leave the position short the rule by the time it fills. Re-verify it after any partial fill before treating the upside as covered.',
    note:
      "Re-verify the credit rule (total credit at least the call spread's width) using actual fill prices before treating the upside as covered — a rule checked only against mid-quotes can be satisfied on screen and violated the moment the order fills in a moving market. Many traders choose the short put strike first, at a level they would accept owning the stock, and only then size the call spread to meet the credit rule, rather than optimizing the call spread and hoping the put strike works out. This is typically approved at the account tier for a cash-secured put plus a defined-risk spread, since the two components carry different requirements.",
    mistake:
      "The common mistake is checking the credit rule against the mid-quotes used to plan the trade rather than against the prices it actually filled at, which can silently reopen the upside risk the structure was chosen to remove.",
    legs: [
      { action: 'Sell', instrument: 'Put', strike: '160' },
      { action: 'Sell', instrument: 'Call', strike: '190' },
      { action: 'Buy', instrument: 'Call', strike: '195' },
    ],
    caps: {
      profitCap: 'Capped: the total credit received',
      lossCap: 'Open below the short put, to zero',
    },
    grid: grid('NVDA at expiry', [140, 155, 170, 190, 195, 210], (S) =>
      4.25 - putV(S, 160) - clamp(S - 190, 0, 5),
    ),
  },

  'covered-call': {
    slug: 'covered-call',
    mechanics:
      "Requires the 100 shares already held or bought in the same order as a 'buy-write' — most brokers offer that as a single combined ticket, avoiding being filled on the stock and not the call in a fast market. The strike is the only real decision once the shares are owned; a higher strike sells for less premium and leaves more room for the stock to run before being called away. Because assignment can happen any time the call is in the money, not only at expiry, treat the shares as sold the moment the strike is comfortably exceeded.",
    note:
      "Selling calls against shares held in a tax-advantaged account avoids the wash-sale and short-term-gain complications that repeated assignment can create in a taxable one, which is one reason this structure is disproportionately common in retirement accounts. Rolling the call out and up before expiry — buying back the near option and selling a later, higher-strike one — is the standard way to keep the shares through a rally that would otherwise trigger assignment, usually at a net debit. Selling calls against only part of a holding, rather than the whole position, is a common way to keep some uncapped upside while still collecting some premium.",
    mistake:
      "The common mistake is selling calls against a core long-term holding and then being surprised at assignment during a rally, treating a structure built to cap upside as though the upside were still fully open.",
    legs: [
      { action: 'Hold', instrument: 'Shares ×100', strike: '—' },
      { action: 'Sell', instrument: 'Call', strike: '240' },
    ],
    caps: {
      profitCap: 'Capped: the strike plus the premium',
      lossCap: 'The full downside of the shares, less the premium',
    },
    grid: grid('AAPL at expiry', [180, 200, 220, 240, 260, 280], (S) =>
      S - 220 + 4.1 - callV(S, 240),
    ),
  },

  'cash-secured-put': {
    slug: 'cash-secured-put',
    mechanics:
      "One leg, but the ticket is really the leg plus the cash: most brokers will not let the order go live without the full strike value already set aside, which is what 'secured' means operationally rather than just economically. Selling further out of the money lowers both the premium and the odds of owning the stock; selling at the money maximizes premium collected per day at the cost of a near-even chance of assignment. If assignment is genuinely unwanted, this is the wrong strike to be selling, not a risk to manage after the fact.",
    note:
      "The cash set aside earns money-market or sweep interest at most brokers while the position is open, which is a real part of the return and is easy to leave out of a quick premium-over-notional calculation. Selling the same strike repeatedly as it expires unassigned, and re-selling after assignment once the shares are owned, is the standard way this is run as an ongoing income position rather than a one-off trade. Account approval for this is typically the same tier as a covered call, since the risk profile — full downside of owning the stock, less a modest credit — is identical.",
    mistake:
      "The common mistake is selling a put on a name you would not actually want to own, treating the premium as free money rather than as the price of a real, if discounted, purchase obligation.",
    legs: [{ action: 'Sell', instrument: 'Put', strike: '560' }],
    caps: {
      profitCap: 'Capped: the premium received',
      lossCap: 'Bounded: strike minus premium, if the stock goes to zero',
    },
    grid: grid('SPY at expiry', [500, 520, 540, 560, 580, 600], (S) => 5.6 - putV(S, 560)),
  },

  'protective-put': {
    slug: 'protective-put',
    mechanics:
      'One leg added to an existing holding, so the only decision on the ticket is the strike and the expiry — but the strike choice is really about how much of the current unrealized gain, if any, you are willing to let the floor sit below. A shorter-dated put costs less per trade but has to be rolled more often, and each roll re-prices against whatever implied volatility is current at the time, which is exactly the opposite of a cost you control by choosing dates in advance.',
    note:
      "Buying protection after a decline means paying elevated implied volatility for it, so many holders set a standing rule to buy the put before a specific date or event rather than reacting to a drop already underway, which is usually the more expensive moment to insure. A married put — buying the shares and the put on the same day — is treated differently for the holding-period clock in some tax jurisdictions than a put purchased against shares already held; check the specific rule before assuming the position resets nothing. Rolling protection down as the underlying rises locks in some of the gain without fully removing the hedge.",
    mistake:
      "The common mistake is buying protection only after a decline has already started, paying the elevated implied volatility that decline itself created rather than insuring while premiums were still ordinary.",
    legs: [
      { action: 'Hold', instrument: 'Shares ×100', strike: '—' },
      { action: 'Buy', instrument: 'Put', strike: '165' },
    ],
    caps: {
      profitCap: 'Uncapped, less the premium paid',
      lossCap: 'Capped: the floor set by the strike',
    },
    grid: grid('NVDA at expiry', [130, 150, 165, 180, 200, 220], (S) =>
      S - 170 + putV(S, 165) - 6.8,
    ),
  },

  collar: {
    slug: 'collar',
    mechanics:
      "Three components on one position: the shares already held, a put bought, a call sold — usually entered as a combined options order against the existing stock rather than as two separate legs. Choosing the two strikes so the premiums roughly offset is a search, not a formula; moving either strike by one increment changes the net cost and the width of the fenced range together. Because the call obligates you to sell at its strike, check the position's cost basis against that strike before entering — a collar struck below the cost basis locks in a loss if it is ever exercised.",
    note:
      "Because the call obligates a sale at its strike, running a collar in a tax-advantaged account avoids forcing a taxable disposal if the shares are ever called away — the same reason covered calls concentrate there. Choosing the two strikes equidistant from the current price is a common starting point, then adjusting one to bring the net cost near zero; the resulting asymmetry is a real choice about which side to protect more, not an artifact of the search. A collar entered around a known lock-up expiry or blackout date is usually sized to cover exactly that window, then removed once trading restrictions lift.",
    mistake:
      "The common mistake is setting the call strike below the cost basis to squeeze more premium out of the structure, which locks in a loss on assignment even though the trade nets a small credit.",
    legs: [
      { action: 'Hold', instrument: 'Shares ×100', strike: '—' },
      { action: 'Buy', instrument: 'Put', strike: '560' },
      { action: 'Sell', instrument: 'Call', strike: '605' },
    ],
    caps: { profitCap: 'Capped: the call strike', lossCap: 'Capped: the put strike' },
    grid: grid('SPY at expiry', [520, 540, 560, 580, 605, 625], (S) =>
      S - 560 + putV(S, 560) - callV(S, 605) - 0.15,
    ),
  },

  'risk-reversal': {
    slug: 'risk-reversal',
    mechanics:
      'Two legs, opposite sides of the market, usually quoted and filled as a single combined order the way a vertical is, which avoids being caught short the put with no long call if the market moves between two separate fills. The two strikes are chosen independently, unlike a vertical\'s shared expiry with linked strikes, so there is no fixed relationship between them beyond both being out of the money. Margin on the short put is calculated the same way a naked short put\'s margin is, regardless of the long call sitting beside it on the same ticket.',
    note:
      "Margin on the short put dominates the account requirement here, calculated the same way a naked short put's margin is regardless of the long call sitting on the same ticket — a broker's margin calculator, not the zero or near-zero net premium, is the number that determines whether the position fits the account. Because it behaves like leveraged stock outside the strikes, many traders size it by the notional exposure it creates above the call strike, not by the small premium it costs to open. A market-wide selloff moves the short put further from the money at the same time margin requirements on it typically rise, which is the scenario this position handles worst.",
    mistake:
      "The common mistake is treating a near-zero net premium as a near-zero risk, when the margin and the downside exposure below the put strike are exactly as large as a naked short put's on their own.",
    legs: [
      { action: 'Sell', instrument: 'Put', strike: '550' },
      { action: 'Buy', instrument: 'Call', strike: '610' },
    ],
    caps: { profitCap: 'Uncapped above the call strike', lossCap: 'Large, bounded only by zero' },
    grid: grid('SPY at expiry', [500, 525, 550, 580, 610, 640], (S) =>
      callV(S, 610) - putV(S, 550) + 0.2,
    ),
  },

  'calendar-spread': {
    slug: 'calendar-spread',
    mechanics:
      "Two options at the same strike, different expiries, usually filled as a single calendar order — most platforms support this directly, which matters because legging in exposes the position to the underlying moving between the two fills at two different implied volatilities. Because the position's value at the front expiry depends on the back month's implied volatility at that moment, not on anything fixed today, there is no single 'right' width or strike — only a most-likely level, chosen from where the underlying is expected to sit.",
    note:
      "Because the position has no closed-form maximum, many traders manage it by a target debit recovered — commonly closing once the position is worth some multiple of what was paid — rather than by a price target on the underlying, since the underlying finishing exactly at the strike is the best case and cannot be relied on. Early assignment on the short near leg, if it happens, leaves a long back-month option against a short stock or futures position rather than against nothing, which is a different and usually undesired exposure. This is typically approved at a spread-trading account tier, similar to a vertical, despite the model-dependent payoff.",
    mistake:
      "The common mistake is holding to the front expiry expecting the theoretical maximum, which requires the underlying to sit exactly at the strike — a coincidence, not a plan.",
    legs: [
      { action: 'Sell', instrument: 'Call (14d)', strike: '580' },
      { action: 'Buy', instrument: 'Call (49d)', strike: '580' },
    ],
    caps: {
      profitCap: 'No closed form — model-dependent',
      lossCap: 'Limited to the net debit paid',
    },
    noClosedForm: true,
  },

  'diagonal-spread': {
    slug: 'diagonal-spread',
    mechanics:
      "Two options at two different strikes and two different expiries — one order on most platforms, structurally similar to the calendar spread's ticket but with an added strike decision. The strike gap is chosen first, based on how much directional lean is wanted, and the expiry gap second, based on how much decay the short leg should harvest before the position needs revisiting; treating the two choices as independent avoids accidentally building a stock-replacement position when a pure calendar was intended.",
    note:
      "The 'poor man's covered call' variant — a deep in-the-money long-dated call standing in for shares — ties up meaningfully less capital than owning the stock outright, which is the entire appeal, but it also means the position can be closed out by the option's own bid/ask liquidity rather than by the (usually deeper) liquidity of the underlying shares. Rolling the short near leg forward as it approaches expiry, repeatedly, is how this is run as an ongoing position rather than a single trade; each roll is its own combined order with its own execution cost. Assignment on the short leg against a long-dated back leg is not a closed position and may require the capital to hold the resulting stock or futures at short notice.",
    mistake:
      "The common mistake is choosing the strike gap and the expiry gap as one combined decision rather than two, which tends to build an accidental stock-replacement position out of what was meant as a pure calendar.",
    legs: [
      { action: 'Sell', instrument: 'Call (21d)', strike: '595' },
      { action: 'Buy', instrument: 'Call (60d)', strike: '585' },
    ],
    caps: {
      profitCap: 'No closed form — model-dependent',
      lossCap: 'Bounded only if the long leg dominates at every price',
    },
    noClosedForm: true,
  },

  /* ================================ Futures ============================== */

  'futures-outright': {
    slug: 'futures-outright',
    mechanics:
      "The simplest futures order there is: one contract, one side, no strike and no expiry decision beyond which delivery month to hold. The real decision is the roll — most participants close the position before first notice date and open the same size in the next active month, and the cost of that roll is set by the calendar spread between the two months, not by anything on this ticket. Check the contract's tick size and point value before sizing; they vary by product and are not the same across the futures listed here.",
    note:
      "Position size in futures is naturally set by the number of contracts, and one contract is already a large notional exposure on most index and commodity products — sizing by 'how many contracts feels comparable to my usual stock position' routinely produces far more leverage than intended. Daily variation margin means a losing position generates real cash calls before the trade thesis has had time to play out, so the account needs spare cash beyond the initial margin, not merely the initial margin itself. Stop orders on futures execute continuously through the overnight session, unlike many equity accounts' day-session-only stops.",
    mistake:
      "The common mistake is sizing contracts by how a position 'feels' relative to an equity account rather than by the actual notional and point value, which is routinely far larger than it appears.",
    legs: [{ action: 'Buy', instrument: 'ES future', strike: '5800' }],
    caps: {
      profitCap: 'Uncapped, marked to market daily',
      lossCap: 'Bounded by zero (long); unlimited (short)',
    },
    grid: futuresGrid('ES at expiry', [5600, 5700, 5800, 5900, 6000, 6100], (S) => (S - 5800) * 50),
  },

  'futures-spread': {
    slug: 'futures-spread',
    mechanics:
      'Entered as a single exchange-recognized spread order rather than two separate futures trades, which is what earns the reduced margin — legging in manually gets charged the full margin on both legs until the exchange recognizes the pair. The spread\'s own bid/ask is usually tighter, relative to its value, than either outright leg\'s bid/ask, because market makers quote the differential directly rather than two independent prices. Rolling an existing calendar position is the same order type as opening a fresh spread; there is no separate "roll" ticket.',
    note:
      "The margin relief this earns from being entered as a single spread order disappears the moment either leg is closed independently, so exiting a spread as two separate orders briefly re-exposes the full outright margin on whichever leg is still open. Many venues quote the spread's own bid/ask directly rather than requiring the two legs to be priced separately, which is usually the tighter and more reliable way to see the real cost of entering or exiting. Because leverage here is much higher per dollar of margin than an outright position, sizing by margin required rather than by notional exposure understates the risk if the spread relationship itself breaks down.",
    mistake:
      "The common mistake is legging out of the two contracts separately during an exit, which briefly loses the margin offset and the tight combined pricing that made the spread attractive to hold as one position.",
    legs: [
      { action: 'Buy', instrument: 'ES Sep future', strike: '5800' },
      { action: 'Sell', instrument: 'ES Dec future', strike: '5845' },
    ],
    caps: {
      profitCap: 'Bounded by how far the differential can move',
      lossCap: 'Bounded the same way, the other direction',
    },
    grid: futuresGrid('Spread (Sep − Dec)', [-75, -60, -45, -30, -15, 0], (D) => (D - -45) * 50),
  },

  'futures-calendar-spread': {
    slug: 'futures-calendar-spread',
    mechanics:
      "Same order type as any futures spread — one ticket, two delivery months, reduced margin from the exchange recognizing the offset. The near leg's first notice date is the real deadline on this position if the product is physically delivered; the spread has to be closed or rolled before it, regardless of where the differential sits. Because the position is quoted directly as the spread value rather than as two prices, watching that single number is usually more useful day to day than watching either outright leg.",
    note:
      "The near leg's first notice date is a hard deadline in a physically delivered product — holding a short near leg past it, rather than rolling or closing, can create a delivery obligation neither side of the trade was meant to take on. Because contango is capped by the cost of storage and financing while backwardation is not, position sizing here is often asymmetric: smaller size when positioned for the capped side, more room allowed when positioned for the side with no structural ceiling. The spread is quoted directly as a single number at most venues, which is the figure to watch rather than either outright leg.",
    mistake:
      "The common mistake is holding a short near-month leg on a physically delivered product past its first notice date, turning a spread position into an unintended delivery obligation.",
    legs: [
      { action: 'Buy', instrument: 'CL Dec future', strike: '78.40' },
      { action: 'Sell', instrument: 'CL Jun future', strike: '80.10' },
    ],
    caps: {
      profitCap: 'Capped by the cost of carry, in contango',
      lossCap: 'Not capped, in backwardation',
    },
    grid: futuresGrid('Spread (Dec − Jun)', [-3.7, -2.7, -1.7, -0.7, 0.3, 1.3], (D) =>
      (D - -1.7) * 1000,
    ),
  },

  'futures-intercommodity-spread': {
    slug: 'futures-intercommodity-spread',
    mechanics:
      "Three separate futures legs at a fixed ratio, usually available as a single 'crack spread' order on exchanges that list the combination directly — check whether the venue offers the ratio as one ticket before legging in manually, since the ratio itself is the entire point of the trade. Contract sizes differ across the three products, which is exactly why a ratio like 3:2:1 exists rather than a simple 1:1:1; using the wrong contract counts turns this into an unintended outright position in whichever product is over- or under-weighted.",
    note:
      "Getting the contract ratio right is the whole trade — a 3:2:1 crack spread entered as 1:1:1 is not a smaller version of the same position, it is a different and unintended outright bet weighted toward whichever leg is under-hedged relative to its true ratio. Margin is set on the combination where the exchange recognizes it as a defined spread; legging in the three contracts separately at full outright margin on each defeats the purpose of trading the relationship rather than the individual legs. Seasonal patterns in the ratio are well known and already reflected in the price by the time they appear in a seasonal chart.",
    mistake:
      "The common mistake is entering the three legs at a round 1:1:1 ratio for simplicity, which is not a smaller version of the crack spread but a different, unhedged outright position.",
    legs: [
      { action: 'Buy ×3', instrument: 'Crude oil future', strike: '—' },
      { action: 'Sell ×2', instrument: 'Gasoline future', strike: '—' },
      { action: 'Sell ×1', instrument: 'Heating oil future', strike: '—' },
    ],
    caps: {
      profitCap: "Bounded by the processing margin's own ceiling",
      lossCap: 'Bounded by the margin turning negative',
    },
    ratioTable: [
      { role: 'Input', instrument: 'Crude oil', ratio: '3 contracts, long' },
      { role: 'Output', instrument: 'Gasoline', ratio: '2 contracts, short' },
      { role: 'Output', instrument: 'Heating oil', ratio: '1 contract, short' },
    ],
  },

  'covered-futures-call': {
    slug: 'covered-futures-call',
    mechanics:
      "An option on the future (an FOP), sold against an existing long futures position — the two are separate tickets on most platforms, since 'buy-write' order types are built for equities and rarely extend to futures options. Options on futures are frequently American-style and can be assigned into a futures position before expiry, at any point once the call is in the money, which is worth checking against the specific product's contract terms before assuming European-style exercise.",
    note:
      "Options on futures often settle American-style, meaning assignment can arrive the moment the call is in the money rather than only at its own expiry, which is a meaningfully different risk than an equity covered call's mostly-at-expiry assignment pattern. The premium received does not reduce the futures leg's own margin requirement, so a fall large enough to trigger a margin call arrives regardless of how much premium has been collected — track the futures margin and the option premium as two separate numbers, not one netted figure. Check the option's own expiry against the futures contract's expiry before assuming they match; they frequently do not.",
    mistake:
      "The common mistake is assuming European-style exercise on the short call, when many options on futures settle American-style and can be assigned into a futures position the moment the call is in the money.",
    legs: [
      { action: 'Buy', instrument: 'ES future', strike: '5800' },
      { action: 'Sell', instrument: 'ES 5900 call (FOP)', strike: '—' },
    ],
    caps: {
      profitCap: 'Capped: the strike plus the premium, in points',
      lossCap: 'The full downside of the future, less the premium',
    },
    grid: futuresGrid('ES at expiry', [5600, 5700, 5800, 5900, 6000, 6100], (S) =>
      50 * (S - 5800 + 42 - callV(S, 5900)),
    ),
  },

  'futures-basis-arbitrage': {
    slug: 'futures-basis-arbitrage',
    mechanics:
      'Two legs in two different markets — a cash purchase and a futures sale — so this is the one structure here that is not a single options or futures ticket but a coordinated pair across a cash desk and a futures account. Financing the cash leg is usually the binding constraint in practice, not the futures margin, and the rate used to compute the carry should be the actual funding rate available, not a benchmark rate that may not be accessible at the size being traded.',
    note:
      "The position is sized by how much cash and storage capacity are actually available to carry to delivery, not by how attractive the basis looks on a screen — a basis trade that cannot be carried to convergence is a directional bet wearing an arbitrage's clothes. Financing costs are typically a floating rate tied to the broker's or bank's own funding cost, and a rate that rises after entry shrinks a locked-in-looking profit in real time even though the price risk is fully hedged. This is largely an institutional trade in practice, for exactly that reason — the operational and funding capacity is the actual barrier to entry, not the arithmetic.",
    mistake:
      "The common mistake is entering the trade on an attractive basis without confirming the storage and funding capacity to actually carry it to convergence, turning a hedged arbitrage into an unhedged directional bet.",
    legs: [
      { action: 'Buy', instrument: 'Gold (spot)', strike: '2650' },
      { action: 'Sell', instrument: 'Gold future', strike: '2704' },
    ],
    caps: {
      profitCap: 'Fixed at entry: basis less cost of carry',
      lossCap: 'Small in principle; financing and unwind risk in practice',
    },
    sensitivityTable: {
      axisLabel: 'Financing rate (annualised)',
      rows: [0.026, 0.031, 0.036, 0.041, 0.046].map((rate) => ({
        label: `${(rate * 100).toFixed(1)}%`,
        // Basis at entry (2704 - 2650 = 54) less six months of carry at `rate`.
        value: money(2704 - 2650 - 2650 * rate * 0.5),
      })),
    },
  },
};

export function getCalculatorExtra(slug: string): CalculatorExtra | undefined {
  return CALCULATOR_EXTRAS[slug];
}
