/**
 * Per-strategy copy for the TOOL pages, `/calculator/<slug>`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Why this exists, and why it is a second content file rather than four more
 * fields on `strategy-guides.ts`:
 *
 * The 2026-08-16 AdSense flag was fixed by moving every written word onto
 * `/guides/<slug>` and leaving the calculator screens as the tool alone. That
 * was right about ADVERTISING and was never checked against SEARCH. Measured
 * 2026-09-16, the 26 `/calculator/<slug>` pages have a median 6-gram Jaccard
 * similarity of 0.978 against each other — they differ by a heading and
 * nothing else — while each declares itself canonical and each is in the
 * sitemap. So the sitemap asks Google to index twenty-six copies of one page,
 * and Search Console says so.
 *
 * Distinct titles and one authored paragraph per page is the smallest thing
 * that fixes that. It changes nothing about advertising: these pages are
 * outside `/guides/<slug>`, so they ship no Google ad code at all, and
 * `check-export.mjs` proves that on the emitted bytes.
 *
 * Two rules the entries below are written to, both enforced:
 *
 *   NOT A TEMPLATE. A paragraph with the strategy name substituted into it is
 *   what produced the original violation in the first place. Each lede has to
 *   say something true of that structure and false of its siblings —
 *   construction, the reason somebody reaches for it, and the way it hurts
 *   people.
 *
 *   NOT THE GUIDE'S SENTENCES. The article at `/guides/<slug>` already opens
 *   with a lede. Copying it here would swap duplication BETWEEN calculator
 *   pages for duplication BETWEEN a calculator page and its own guide, which
 *   is the same problem with one more URL in it. These are condensed and
 *   independently worded, and `check-export.mjs` asserts no sentence of one
 *   appears in the other.
 */

export interface CalculatorPageCopy {
  slug: string;
  /** `<title>`. The strategy name plus what this screen actually computes. */
  title: string;
  /** Meta description. One sentence, specific to this structure. */
  description: string;
  /** `<h1>`, rendered in the workspace header. The phrase people search. */
  heading: string;
  /** 80-120 words of prose, rendered below the tool. Distinct per strategy. */
  lede: string;
}

export const CALCULATOR_PAGES: Record<string, CalculatorPageCopy> = {
  /* ============================ Single options =========================== */

  'long-call': {
    slug: 'long-call',
    title: 'Long Call Calculator — Breakeven, Decay and Payoff',
    description:
      'Price a long call on live option-chain quotes: premium, breakeven, the delta and gamma profile, and what time decay costs you each day you hold it.',
    heading: 'Long Call Calculator',
    lede:
      'One call, bought outright. The debit paid is the whole of the risk and it is known before the order goes in, while above the strike the contract gains with the underlying and nothing caps it. People reach for it when a move is expected inside a definite window and they want the worst case written down in advance rather than discovered later. The difficulty is that a call has to be right twice — about which way and about by when — because time value drains whether or not the move arrives. Set the strike and the days here, then read the breakeven: it is the number that says how big the move has to be.',
  },

  'long-put': {
    slug: 'long-put',
    title: 'Long Put Calculator — Downside Payoff and Hedge Cost',
    description:
      'Model a long put against live quotes: premium, breakeven, the floor it puts under a holding, and how much of the position value decay removes before expiry.',
    heading: 'Long Put Calculator',
    lede:
      'A single put, bought for a debit that is also the worst case. Below the strike it gains as the underlying falls, and because a price stops at zero the profit is large but finite — unlike a call, whose upside has no such stop. It is bought either as an outright bearish position or, far more often, as cover for shares already held. What makes it expensive is that everyone wants it: demand for downside protection is persistent, so puts usually trade at higher implied volatility than the calls beside them. Set the strike and the expiry here and weigh the premium against the fall you are actually insuring against.',
  },

  'call-spread': {
    slug: 'call-spread',
    title: 'Bull Call Spread Calculator — Debit, Cap and Breakeven',
    description:
      'Price a bull call spread leg by leg: net debit, capped profit at the short strike, breakeven, and how widening the strikes trades cost against upside.',
    heading: 'Bull Call Spread Calculator',
    lede:
      'Buy one call, sell a higher one on the same expiry, and let the credit from the short leg pay down the debit on the long. Cost, breakeven and maximum loss all come down together; the profit stops dead at the upper strike. It suits a view that has a target attached to it — up, but to about here — and it is the usual answer when an outright call looks too expensive to justify. The mistake it invites is narrowness: a spread struck too close caps the gain before the expected move has finished running. Move the two strikes here and watch the debit and the ceiling trade against one another.',
  },

  'put-spread': {
    slug: 'put-spread',
    title: 'Bear Put Spread Calculator — Cost and Bounded Downside',
    description:
      'Price a bear put spread on live quotes: net debit, the profit floor set by the lower strike, breakeven, and the decay both legs carry into expiry.',
    heading: 'Bear Put Spread Calculator',
    lede:
      'Buy a put, sell a lower-strike put beneath it, same expiry. The short leg refunds part of the premium and in exchange the payoff goes flat below its strike, so this is a bounded bearish position rather than an open-ended one. It fits a decline expected to a level rather than to zero, and it stops you paying full premium for a tail nobody in the trade actually believes in. Both legs decay, which softens the usual bleed on a long option without removing it. Price the pair here and check where the breakeven falls relative to the support level you had in mind when you opened the chain.',
  },

  'bull-put-spread': {
    slug: 'bull-put-spread',
    title: 'Bull Put Spread Calculator — Credit and Defined Risk',
    description:
      'Model a short put spread: credit received, maximum loss between the strikes, breakeven, and the short leg delta as a rough measure of the odds.',
    heading: 'Bull Put Spread Calculator',
    lede:
      'Sell a put, buy a cheaper one below it, and take the difference as a credit on day one. Nothing more will ever be earned: the entire trade is keeping that credit by having the underlying finish above the short strike. The long put is insurance on your own short, turning an obligation with no natural limit into a fixed number you can size a position against. Being an income structure, the arithmetic looks deliberately unattractive — many small wins against occasional large losses — and the discipline lives entirely in size. Price both strikes here and read the maximum loss before the credit, not after it.',
  },

  'bear-call-spread': {
    slug: 'bear-call-spread',
    title: 'Bear Call Spread Calculator — Credit, Cap and Risk',
    description:
      'Price a short call spread against live quotes: credit collected, the loss if the market runs through the short strike, breakeven and probability of profit.',
    heading: 'Bear Call Spread Calculator',
    lede:
      'Sell a call and buy a further-out call above it as cover. The credit arrives at entry and is kept if the underlying settles below the short strike, while the long call fixes what a rally through it can cost. It expresses a ceiling rather than a forecast — the market does not have to fall, only to fail to climb past a level you have chosen. The characteristic error is picking a strike that looks comfortably far away in dollars and is close in standard deviations. Set the strikes here and read the short leg delta beside the credit; it is the nearest thing to an odds estimate on the screen.',
  },

  straddle: {
    slug: 'straddle',
    title: 'Long Straddle Calculator — Two Breakevens and Vega',
    description:
      'Price a long straddle: combined premium, the upper and lower breakeven, net vega, and how much an implied volatility crush costs after the event passes.',
    heading: 'Long Straddle Calculator',
    lede:
      'A call and a put at the same strike and expiry, bought together. The position starts close to delta-neutral and pays on a large move in either direction, which makes it the instrument for an event whose outcome is unknown but whose size is not. Both legs carry full time value, so you pay twice and only one of them can finish in the money — the move has to clear the pair before anything is earned. Implied volatility almost always falls once the uncertainty resolves, which can turn a correct call on direction into a loss. Price both legs here and read the two breakevens sitting either side of spot.',
  },

  strangle: {
    slug: 'strangle',
    title: 'Long Strangle Calculator — Strike Gap and Breakevens',
    description:
      'Model a long strangle on live quotes: combined debit, the breakeven either side of the strike gap, and how strike width trades cost against the move required.',
    heading: 'Long Strangle Calculator',
    lede:
      'Buy an out-of-the-money call above the market and an out-of-the-money put below it. Both are cheaper than the at-the-money contracts a straddle uses, so the same capital buys more of them — and the gap between the strikes has to be crossed before a cent is earned. It is the choice when a large move is expected but the timing or the size of it does not justify a straddle. The strike gap is the whole decision, wider being cheaper and needing more. Set both strikes and the days here, then compare the breakevens against how far this name has actually moved in past windows of the same length.',
  },

  'iron-condor': {
    slug: 'iron-condor',
    title: 'Iron Condor Calculator — Credit, Wings and Range',
    description:
      'Price a four-leg iron condor: total credit, the profitable range between the short strikes, maximum loss at either wing, and the odds of finishing inside it.',
    heading: 'Iron Condor Calculator',
    lede:
      'Four legs on one expiry: a put spread sold below the market and a call spread sold above it. Two credits go in and both are kept if the underlying finishes between the short strikes, so this is a position on a range rather than on a direction. The long wings are what convert two obligations with no natural limit into a loss you can size in advance. Its failure mode is slow and then sudden — a long run of small gains interrupted by one move straight through a wing. Set the four strikes here and read the profitable band against the probability distribution drawn beside it.',
  },

  'iron-butterfly': {
    slug: 'iron-butterfly',
    title: 'Iron Butterfly Calculator — Pinned Range and Credit',
    description:
      'Model an iron butterfly: the credit from the short straddle, wing cost, the narrow band that keeps it, and assignment risk on the at-the-money legs.',
    heading: 'Iron Butterfly Calculator',
    lede:
      'An iron condor with its two short strikes collapsed together at the money: sell the straddle, buy a wing on each side of it. The credit is far larger than a condor pays and the band in which you keep it is far narrower, so this is a statement about where the underlying settles rather than merely that it stays put. Maximum profit occurs at one price and falls away steeply on both sides. Short at-the-money options also carry real early-assignment risk as expiry approaches. Price the body and the wings here and look at the width of the profitable band before you look at the size of the credit.',
  },

  butterfly: {
    slug: 'butterfly',
    title: 'Call Butterfly Calculator — Peak Strike and Risk Ratio',
    description:
      'Price a call butterfly leg by leg: net debit, the peak payoff at the middle strike, both breakevens, and the reward-to-risk ratio the structure is bought for.',
    heading: 'Call Butterfly Calculator',
    lede:
      'Buy one call below, sell two at a middle strike, buy one above, all equally spaced. The debit is small and the payoff peaks at that middle strike, which is where the appeal lies: a butterfly commonly risks one to make four or five. It states a precise view about where something finishes and is cheap enough to be wrong with it repeatedly. What makes it hard is execution rather than theory — three strikes and four contracts mean four bid/ask spreads, and on a small debit those spreads are a large share of the trade. Price it here and compare the maximum profit against the spread you would really pay.',
  },

  condor: {
    slug: 'condor',
    title: 'Call Condor Calculator — Plateau Width and Debit',
    description:
      'Model a long call condor: net debit, the plateau of maximum profit between the inner strikes, both breakevens, and how body width trades payoff against odds.',
    heading: 'Call Condor Calculator',
    lede:
      'Buy a low call, sell two calls at separated middle strikes, buy a high one. Stretching a butterfly peak into a plateau makes the structure forgiving: the maximum is earned across a band of settlement prices rather than at one of them, and the price of that is a lower maximum for the same debit. Built from calls alone, it pays for the same range an iron condor sells, financed by a debit instead of a credit. Four legs again, so the spread cost is the practical constraint on a small trade. Set the inner and outer strikes here and watch a wider body buy probability with profit.',
  },

  'jade-lizard': {
    slug: 'jade-lizard',
    title: 'Jade Lizard Calculator — Credit Against Call-Spread Width',
    description:
      'Price a jade lizard: total credit, the call-spread width it must exceed to remove upside risk, and the downside a short put leaves open below its strike.',
    heading: 'Jade Lizard Calculator',
    lede:
      'A short out-of-the-money put alongside a short call spread above the market. What defines it is an arithmetic rule rather than a shape: collect a total credit at least as large as the width of the call spread and there is no upside risk at all, because the credit already covers the most that spread can lose. One exposure is left — the underlying falling through the short put — and it is the exposure somebody willing to own the shares has already accepted. Price the three legs here and test the credit against the call-spread width first; below it, the property the structure is chosen for simply is not there.',
  },

  'covered-call': {
    slug: 'covered-call',
    title: 'Covered Call Calculator — Premium, Cap and Assignment',
    description:
      'Model a covered call on 100 shares: premium income, return if called away, the effective cap on the stock, and the downside the shares still carry.',
    heading: 'Covered Call Calculator',
    lede:
      'A hundred shares you already own with a call sold against them. The premium is income and a thin cushion, and in return those shares are committed for sale at the strike for the life of the option. It is the most widely held options position and among the most widely misread: the risk sits in the stock underneath, not in the option, and one or two per cent of premium does not change what a holding can lose. Early assignment around an ex-dividend date is routine rather than exotic. Set the strike and expiry here and read the return if called away next to the drawdown the shares can still take.',
  },

  'cash-secured-put': {
    slug: 'cash-secured-put',
    title: 'Cash-Secured Put Calculator — Credit and Net Cost Basis',
    description:
      'Price a cash-secured put: premium received, the net cost basis if assigned, breakeven, and the yield on the cash the position has to keep aside.',
    heading: 'Cash-Secured Put Calculator',
    lede:
      'Sell a put and hold enough cash to buy the shares if they are put to you. Two outcomes follow, and both are meant to be acceptable: the option expires and the credit is yours, or you are assigned and own the stock at the strike less the premium already received. That makes it an acquisition tool as much as an income one — but only if you would buy at that price with no premium attached, because assignment arrives exactly when the name is weakest. Price the strike and the days here and set the annualised yield against the cash the position has to leave idle.',
  },

  'protective-put': {
    slug: 'protective-put',
    title: 'Protective Put Calculator — Floor Price and Hedge Cost',
    description:
      'Model a protective put over a long holding: the floor its strike sets, the premium that floor costs, breakeven, and the upside left after paying for it.',
    heading: 'Protective Put Calculator',
    lede:
      'Shares held long with a put bought against them. Below the strike the put gains as the stock falls, so the holding has a floor under it; above the strike the upside runs on, less whatever the protection cost. This is insurance in the exact sense of the word, including the part where most premiums paid are never seen again. The real questions are how far below the market to place the floor and how long to insure for, because rolling short-dated protection is expensive and long-dated protection is thin. Price the strike and expiry here and read the protected loss against the premium buying it.',
  },

  collar: {
    slug: 'collar',
    title: 'Collar Calculator — Floor, Cap and Net Cost',
    description:
      'Price a collar over a stock position: the put floor, the call cap that funds it, net debit or credit, and the band of outcomes left between the two.',
    heading: 'Collar Calculator',
    lede:
      'Long shares, a protective put beneath them, and a call sold above to pay for it. The outcome is fenced on both sides and the financing is usually close to free, which matters more than it sounds: protection that has to be funded out of pocket every quarter rarely survives a year. It is the standard structure for a concentrated holding that cannot simply be sold, and it works by handing away upside the holder was not counting on. One number states the whole compromise — the distance between floor and cap. Set both strikes here and read the net debit or credit alongside it.',
  },

  'risk-reversal': {
    slug: 'risk-reversal',
    title: 'Risk Reversal Calculator — Zero-Cost Strikes and Downside',
    description:
      'Model a risk reversal: the short put that funds the long call, net cost near zero, and the open-ended loss below the put strike the financing creates.',
    heading: 'Risk Reversal Calculator',
    lede:
      'Sell an out-of-the-money put and spend what it pays on an out-of-the-money call. Between the strikes little happens; outside them the position behaves much like a leveraged long. It can often be opened for no net premium, and that apparent cheapness is the thing to understand rather than the thing to like: the call is funded by accepting an obligation to buy shares well below the market, so the downside runs on exactly as a naked short put does. Price both strikes here and look first at the loss below the put strike, which is the number a zero-cost framing quietly hides.',
  },

  'calendar-spread': {
    slug: 'calendar-spread',
    title: 'Calendar Spread Calculator — Modelled Value at Front Expiry',
    description:
      'Price a calendar spread: net debit, the modelled value of the back month when the front expires, and the volatility exposure the two legs leave behind.',
    heading: 'Calendar Spread Calculator',
    lede:
      'Short the front month and own a later one at the same strike. The near leg gives up time value faster than the back leg does, and that difference is the whole of the return. It is the one structure on this site whose payoff is not a set of straight lines: when the front expires the back is still alive and has to be valued by a model, so there is no arithmetic maximum to quote. It wants the underlying to sit still and it gains if implied volatility in the back month rises. Set both expiries here and read the modelled value at the front expiry rather than a payoff diagram.',
  },

  'diagonal-spread': {
    slug: 'diagonal-spread',
    title: 'Diagonal Spread Calculator — Strike Lean and Time Decay',
    description:
      'Model a diagonal spread across two expiries and two strikes: net debit, the directional lean the strike gap adds, and what early assignment on the front leg costs.',
    heading: 'Diagonal Spread Calculator',
    lede:
      'Sell a near-dated option at one strike and buy a longer-dated one at another. Both dimensions are mismatched on purpose: the expiry gap harvests decay, the strike gap adds a directional lean, and how far apart the strikes sit decides which of the two dominates the result. Widened out with a deep in-the-money back month it becomes a stock substitute funded by selling the front month again and again, behaving much like covered stock on a fraction of the capital. Price both legs here and check what the back month is worth if the front is assigned early, which is the case that breaks the financing.',
  },

  /* ================================ Futures ============================== */

  'futures-outright': {
    slug: 'futures-outright',
    title: 'Futures Outright Calculator — Tick Value, Margin and P&L',
    description:
      'Price a long or short futures position: contract multiplier, tick value, initial margin, and the profit or loss on a move of a given number of points.',
    heading: 'Futures Outright Calculator',
    lede:
      'One futures contract, long or short. There is no premium, no strike and no decay — the payoff is a straight line through the entry price — so everything about the position comes from contract size and leverage. An E-mini S&P contract carries index exposure of fifty times the index level on margin worth a small fraction of that, and the same arithmetic runs in both directions with nothing to soften it. Losses are not bounded by what was posted, and variation margin settles in cash daily. Set the contract and the move here, then read what a single point is worth before deciding how many to hold.',
  },

  'futures-spread': {
    slug: 'futures-spread',
    title: 'Futures Spread Calculator — Differential and Spread Margin',
    description:
      'Model a two-leg futures spread: the differential between the contracts, the reduced spread margin exchanges charge, and the move that would exhaust it.',
    heading: 'Futures Spread Calculator',
    lede:
      'Long one futures contract and short a related one, so what matters is the gap between them rather than where either goes on its own. Shocks common to both legs largely cancel, which is why exchanges margin a recognised spread far more cheaply than the same two contracts held apart. That relief is also the trap: carrying the position costs little, so it is easy to hold size the differential can move against faster than it seems it should. Set both legs here and read the margin requirement next to the widening of the spread that would consume it.',
  },

  'futures-calendar-spread': {
    slug: 'futures-calendar-spread',
    title: 'Futures Calendar Spread Calculator — Contango and Carry',
    description:
      'Price a futures calendar spread between two delivery months: the differential, the implied carry it embeds, and whether the curve is in contango or backwardation.',
    heading: 'Futures Calendar Spread Calculator',
    lede:
      'Long one delivery month and short another in the same product. The level of the curve drops out and its shape is what is left: the position pays when the differential between the two months widens or narrows, largely irrespective of the outright price. Every roll of a long-dated futures position is this trade, whether the person doing it thinks of it that way or not. Storage, financing and seasonality drive the differential, so contango and backwardation are the working vocabulary here rather than bullish and bearish. Set the two months here and read the spread and the carry implied between them.',
  },

  'futures-intercommodity-spread': {
    slug: 'futures-intercommodity-spread',
    title: 'Inter-Commodity Spread Calculator — Crack and Crush Ratios',
    description:
      'Model an inter-commodity futures spread at its correct leg ratio: the processing margin it represents, the differential in trade units, and combined margin.',
    heading: 'Inter-Commodity Spread Calculator',
    lede:
      'Long one product and short a different but economically tied one — crude against refined products, soybeans against meal and oil, grain against livestock. The differential normally stands for a processing or substitution margin somebody in the physical market genuinely earns, and that is what separates it from a statistical pairs trade with a pretty chart. It breaks when the physical link breaks: an outage, a change in yields, a shift in ratios moves the spread for reasons no price history saw coming. Set the leg ratios here and read the differential in the units a processor would use, not as the difference of two screen prices.',
  },

  'covered-futures-call': {
    slug: 'covered-futures-call',
    title: 'Covered Futures Call Calculator — Premium on a Margined Long',
    description:
      'Price a call written against a long futures contract: premium collected, the capped gain above the strike, and the margin the futures leg still requires.',
    heading: 'Covered Futures Call Calculator',
    lede:
      'A long futures position with a call on that same future sold against it. The economics resemble an equity covered call — premium received, upside surrendered above the strike — with one difference that changes the risk completely: what sits underneath is a margined contract, not fully paid shares. The premium softens a small decline and does nothing at all about variation margin on a large one. Options on futures also settle into a futures position rather than into stock, which surprises people once. Set the futures leg and the short call here and read the capped gain against the margin the long leg still ties up.',
  },

  'futures-basis-arbitrage': {
    slug: 'futures-basis-arbitrage',
    title: 'Cash and Carry Basis Trade Calculator — Implied Carry Yield',
    description:
      'Model a cash and carry basis trade: the futures premium over spot, financing and storage over the holding period, and the implied carry yield that remains.',
    heading: 'Futures Basis Trade Calculator',
    lede:
      'Buy the asset in the cash market, sell the futures against it, and hold both until the basis converges at delivery. The return is fixed the moment the trade goes on: the futures premium over spot, less financing, storage and insurance for the days in between. This is the mechanism that keeps futures priced near fair value, so the mispricings it feeds on are small and execution costs decide whether anything is left. The real risk lives in the carry assumptions and in the cash leg rather than in the price. Set spot, the futures price and the days to delivery here and read the implied carry yield.',
  },
};

/**
 * Copy for one calculator page, or `undefined` for a slug with none.
 *
 * Undefined is a supported state: the route falls back to deriving a title and
 * heading from the slug, exactly as it did before this file existed, so a new
 * strategy ships a working page rather than a build failure. What it does NOT
 * ship is a distinct one — `calculator-pages.test.ts` asserts every exported
 * slug has an entry, so the gap is caught before the page is published.
 */
export function getCalculatorPageCopy(slug: string): CalculatorPageCopy | undefined {
  return CALCULATOR_PAGES[slug];
}
