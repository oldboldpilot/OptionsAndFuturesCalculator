/**
 * Editorial content for the per-strategy pages.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Why this file exists, stated plainly so nobody deletes it as boilerplate:
 * every `/calculator/<slug>` page rendered the identical `StrategyWorkspace`
 * with one heading string changed. Measured on the live site, `/long-call` and
 * `/iron-condor` both returned exactly 753 words and differed by the strategy
 * name alone. Twenty-six near-duplicate pages, each carrying advertising and
 * none carrying a sentence anyone could read, is what Google's "ads on screens
 * without publisher-content" policy names, and it is why this site was flagged.
 *
 * So the requirement here is not decoration. Each entry has to be genuinely
 * useful to somebody pricing that structure, and it has to be DIFFERENT from
 * its twenty-five siblings — a template filled with the same paragraph and a
 * substituted name would reproduce the violation in longer form.
 *
 * Accuracy is not optional either. These are payoff identities for real
 * instruments and somebody can act on them. Every max-profit, max-loss and
 * breakeven line below is the closed-form result at EXPIRY, quoted per share;
 * a standard equity option covers 100 shares, so multiply by 100 for one
 * contract. The worked examples are arithmetic on those same identities and
 * each one is checked against the formula above it.
 */

export interface GuideExample {
  /** One sentence naming the position being priced. */
  setup: string;
  /** Label/value pairs. Values are dollars unless the label says otherwise. */
  rows: Array<[string, string]>;
  /** What the numbers do NOT account for. Always present: fees are never in them. */
  note: string;
}

export interface StrategyFaq {
  q: string;
  a: string;
}

export interface StrategyGuide {
  slug: string;
  /** Display name. Matches the heading the workspace renders. */
  name: string;
  /** Directional stance in a few words, e.g. "Bullish — directional". */
  outlook: string;
  /** Whether opening the position pays you or costs you. */
  netCost: 'Debit' | 'Credit' | 'Debit or credit' | 'Margin';
  /** Two or three sentences. The first thing read on the page. */
  lede: string;
  /** The legs, one per line, in the order the ticket builds them. */
  construction: string[];
  maxProfit: string;
  maxLoss: string;
  breakeven: string;
  /** How the position behaves before expiry, which the payoff diagram hides. */
  greeks: string;
  whenToUse: string;
  /** The ways this specific structure hurts people. Not generic warnings. */
  risks: string[];
  example: GuideExample;
  faqs: StrategyFaq[];
}

const FEES =
  'Commissions, exchange fees, financing and the bid/ask spread are excluded. ' +
  'On a multi-leg position the spread is usually the largest of these.';

export const STRATEGY_GUIDES: Record<string, StrategyGuide> = {
  /* ============================ Single options =========================== */

  'long-call': {
    slug: 'long-call',
    name: 'Long Call',
    outlook: 'Bullish — directional',
    netCost: 'Debit',
    lede:
      'The simplest long option. You pay a premium for the right to buy the underlying at a fixed strike until expiry, so the most you can lose is what you paid and the upside is not capped. ' +
      'What makes it harder than it looks is not the payoff but the clock: the position has to be right about direction and about timing, and being right slowly pays nothing.',
    construction: ['Buy 1 call at strike K, expiring at T'],
    maxProfit:
      'Unlimited. Above the strike the position tracks the underlying one-for-one, less the premium paid.',
    maxLoss:
      'The premium paid, in full. Realised anywhere at or below the strike at expiry, where the call expires worthless.',
    breakeven: 'K + premium paid, at expiry.',
    greeks:
      'Long delta, rising from near 0 for a far out-of-the-money strike toward 1 deep in the money and passing about 0.5 at the money. Long gamma, so the delta grows in your favour as the move goes your way. Long vega — a rise in implied volatility helps even with the underlying unchanged. Short theta, and that is the position\'s running cost: decay is roughly proportional to the square root of time remaining, so it accelerates sharply inside the last thirty days for an at-the-money strike.',
    whenToUse:
      'When you expect a move large enough and soon enough to clear both the strike and the premium, and you want the loss bounded by a number you choose in advance. It is also the cheapest way to hold upside exposure through an event you cannot afford to be short of, because the worst case is known before you enter.',
    risks: [
      'You can be right about direction and still lose everything. If the underlying finishes at or below the strike the call expires worthless no matter how much it rose along the way.',
      'Implied volatility crush. Buying a call into a known event — earnings, a rate decision — means paying an inflated premium that collapses once the event passes, which can produce a loss on a favourable move.',
      'Theta accelerates. An at-the-money call bleeds far faster in its last month than its first, so holding for a move that has not started is the most expensive way to be patient.',
    ],
    example: {
      setup: 'SPY trading at 580. Buy the 585 call, 45 days to expiry, for 7.25.',
      rows: [
        ['Debit paid', '$7.25 per share — $725 for one contract'],
        ['Breakeven at expiry', '$592.25 (585 + 7.25)'],
        ['Loss if SPY finishes at or below 585', '$725, the whole premium'],
        ['Profit if SPY finishes at 620', '$2,775 ((620 − 585 − 7.25) × 100)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Should I exercise a profitable call or sell it?',
        a: 'Almost always sell it. Exercising captures only the intrinsic value and throws away whatever time value remains, and it also requires the cash to take delivery of 100 shares per contract. Selling realises intrinsic and time value together.',
      },
      {
        q: 'Why did my call lose money when the stock went up?',
        a: 'Two usual causes. Either implied volatility fell — common straight after an earnings release — and the vega loss outweighed the delta gain, or the move was smaller than the decay over the days you held it. The Greeks panel separates the two.',
      },
      {
        q: 'Which strike should I buy?',
        a: 'That is a trade-off, not a rule. A lower strike costs more but has a higher delta and a nearer breakeven; a higher strike is cheaper and needs a bigger move to pay. Price both here and compare the breakeven against the move you actually expect.',
      },
    ],
  },

  'long-put': {
    slug: 'long-put',
    name: 'Long Put',
    outlook: 'Bearish — directional, or a hedge',
    netCost: 'Debit',
    lede:
      'The right to sell the underlying at a fixed strike until expiry. It is the mirror of the long call with one asymmetry that matters: the underlying can only fall to zero, so the profit is large but bounded, whereas a call\'s is not. ' +
      'It is also the building block of most hedges, because it is the only position that pays more precisely when everything else you own is paying less.',
    construction: ['Buy 1 put at strike K, expiring at T'],
    maxProfit:
      'K − premium paid, reached only if the underlying goes to zero. Large but bounded, unlike a long call.',
    maxLoss: 'The premium paid, realised at or above the strike at expiry.',
    breakeven: 'K − premium paid, at expiry.',
    greeks:
      'Negative delta, between 0 and −1, near −0.5 at the money. Long gamma, so the position gets shorter as the underlying falls and the move compounds. Long vega, which is why puts are expensive precisely when people want them: volatility and falling prices arrive together, so the hedge reprices upward as the risk it covers materialises. Short theta.',
    whenToUse:
      'To express a bearish view with a loss you cap in advance rather than the unbounded one a short stock position carries, or to insure a holding you do not want to sell — for tax reasons, or because you still like it long term. As a hedge its cost is the honest price of the protection, not a fee to be minimised into uselessness.',
    risks: [
      'The premium is a real, recurring cost. Rolling protective puts quarterly can consume a large share of a portfolio\'s return in a market that simply drifts up.',
      'Puts are structurally expensive. Equity index skew prices downside strikes above the equivalent upside strike, so you routinely pay more than a symmetric model implies.',
      'Volatility can fall while the underlying does too. A slow grind down with implied volatility collapsing can leave a put barely profitable despite the direction being right.',
    ],
    example: {
      setup: 'QQQ trading at 500. Buy the 490 put, 60 days to expiry, for 9.40.',
      rows: [
        ['Debit paid', '$9.40 per share — $940 for one contract'],
        ['Breakeven at expiry', '$480.60 (490 − 9.40)'],
        ['Loss if QQQ finishes at or above 490', '$940, the whole premium'],
        ['Profit if QQQ finishes at 450', '$3,060 ((490 − 450 − 9.40) × 100)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Is a long put better than shorting the stock?',
        a: 'It has a different risk shape. The put caps your loss at the premium and needs no borrow, while a short sale has unbounded loss, pays borrow costs and can be recalled. The put pays for that safety with time decay, so the short is cheaper to hold if the fall is slow.',
      },
      {
        q: 'How far out should a protective put be?',
        a: 'Longer expiries cost more in absolute terms but less per day, because time value decays fastest at the end. If the protection is meant to be permanent, longer-dated puts rolled less often usually cost less per unit of cover than short-dated ones rolled constantly.',
      },
    ],
  },

  /* ============================ Vertical spreads ========================= */

  'call-spread': {
    slug: 'call-spread',
    name: 'Bull Call Spread',
    outlook: 'Bullish — moderate, defined range',
    netCost: 'Debit',
    lede:
      'A long call with a higher-strike call sold against it. The short leg pays for part of the long one, which lowers the breakeven and the cost, and in exchange it caps the profit at the higher strike. ' +
      'It is the standard answer to "I think this rises, but not indefinitely, and I do not want to pay full premium to find out".',
    construction: [
      'Buy 1 call at the lower strike K₁',
      'Sell 1 call at the higher strike K₂, same expiry',
    ],
    maxProfit:
      '(K₂ − K₁) − net debit. Reached at or above the upper strike at expiry.',
    maxLoss: 'The net debit paid. Reached at or below the lower strike at expiry.',
    breakeven: 'K₁ + net debit, at expiry.',
    greeks:
      'Net long delta, but far smaller than the outright call — the short leg cancels part of it, and the cancellation grows as the underlying approaches the upper strike. Vega is close to flat, since the two legs have opposing exposures, so this structure is much less sensitive to an implied-volatility collapse than a naked long call. Theta is mildly negative while the underlying sits below the long strike and turns positive once the position is deep in the money, because then it is the short leg that still has time value to lose.',
    whenToUse:
      'When you have a target price rather than an open-ended view. Setting the short strike at your target is the point of the structure: you are selling the part of the distribution you do not believe in and using the proceeds to cheapen the part you do. It is also the better expression when implied volatility is high, because you are buying and selling volatility at once.',
    risks: [
      'The profit is capped, and a large favourable move pays no more than a small one that clears the upper strike. Being spectacularly right earns the same as being adequately right.',
      'Early assignment on the short leg, most often just before an ex-dividend date when the short call is in the money. The long leg still covers you, but you may be left short stock over a weekend.',
      'Two legs means two spreads to cross, on entry and again on exit. On a narrow spread the round-trip friction can be a meaningful share of the maximum profit.',
    ],
    example: {
      setup: 'SPY at 580. Buy the 580 call and sell the 600 call, 45 days out, for a net 7.25 debit.',
      rows: [
        ['Net debit paid', '$7.25 per share — $725 for one contract'],
        ['Width of the spread', '$20.00 (600 − 580)'],
        ['Maximum profit', '$1,275 ((20 − 7.25) × 100), at or above 600'],
        ['Maximum loss', '$725, at or below 580'],
        ['Breakeven at expiry', '$587.25 (580 + 7.25)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'How wide should the spread be?',
        a: 'Width sets the maximum profit and the cost together. A wider spread costs more and behaves more like an outright call; a narrower one is cheaper with a lower ceiling. Choose the short strike from where you think the underlying actually stops, not from the payoff picture.',
      },
      {
        q: 'What happens if both legs finish in the money?',
        a: 'They offset. The long call is exercised and the short is assigned, netting the full width of the spread, which is the maximum profit. Most brokers handle this automatically, but check the assignment policy — some will not auto-exercise if the account cannot support the intermediate stock position.',
      },
    ],
  },

  'put-spread': {
    slug: 'put-spread',
    name: 'Bear Put Spread',
    outlook: 'Bearish — moderate, defined range',
    netCost: 'Debit',
    lede:
      'A long put financed by selling a lower-strike put. It profits as the underlying falls, down to the lower strike, and below that the two legs offset and nothing further is earned. ' +
      'It is the bearish mirror of the bull call spread and is used for the same reason: to buy a directional view without paying full premium for a tail you do not expect to reach.',
    construction: [
      'Buy 1 put at the higher strike K₂',
      'Sell 1 put at the lower strike K₁, same expiry',
    ],
    maxProfit: '(K₂ − K₁) − net debit. Reached at or below the lower strike at expiry.',
    maxLoss: 'The net debit paid. Reached at or above the higher strike at expiry.',
    breakeven: 'K₂ − net debit, at expiry.',
    greeks:
      'Net short delta, damped by the short put and shrinking toward zero as the underlying approaches the lower strike. Vega is close to flat, which matters more here than in the call version: downside strikes carry the steepest skew, so an outright put purchase pays the most inflated volatility on the board and the spread structurally recovers part of it by selling a strike further down that same skew.',
    whenToUse:
      'When you expect a decline to a level you can name — a support price, a valuation floor, a prior gap — rather than a collapse. Because equity puts are expensive, the financing from the short leg is worth more here than the equivalent leg in a bull call spread, and this structure is often the cheapest defined-risk way to be short.',
    risks: [
      'Skew works against the exit as well as the entry. If volatility rises sharply the leg you are short rises faster than the one you are long, so a violent move down can be worth less than the payoff diagram suggests until expiry approaches.',
      'Early assignment on the short put, which is most likely when it is deep in the money and carries little time value, leaving you long stock.',
      'The profit stops at the lower strike. A crash pays exactly the same as a move to your target.',
    ],
    example: {
      setup: 'AAPL at 230. Buy the 230 put and sell the 215 put, 40 days out, for a net 5.10 debit.',
      rows: [
        ['Net debit paid', '$5.10 per share — $510 for one contract'],
        ['Width of the spread', '$15.00 (230 − 215)'],
        ['Maximum profit', '$990 ((15 − 5.10) × 100), at or below 215'],
        ['Maximum loss', '$510, at or above 230'],
        ['Breakeven at expiry', '$224.90 (230 − 5.10)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Why use this instead of just buying a put?',
        a: 'Cost and volatility exposure. The short leg cuts the debit and largely neutralises vega, so you are not relying on implied volatility staying elevated. You give up everything below the lower strike to get that.',
      },
    ],
  },

  'bull-put-spread': {
    slug: 'bull-put-spread',
    name: 'Bull Put Spread',
    outlook: 'Bullish to neutral — income',
    netCost: 'Credit',
    lede:
      'Sell a put and buy a further out-of-the-money put beneath it. You collect a credit up front and keep all of it provided the underlying stays above the short strike; the long put exists solely to cap what happens if it does not. ' +
      'The payoff is the same shape as a bull call spread at the same strikes — this is the credit-financed way of expressing it.',
    construction: [
      'Sell 1 put at the higher strike K₂',
      'Buy 1 put at the lower strike K₁, same expiry',
    ],
    maxProfit: 'The net credit received. Kept in full at or above the short strike at expiry.',
    maxLoss: '(K₂ − K₁) − net credit. Reached at or below the long strike at expiry.',
    breakeven: 'K₂ − net credit, at expiry.',
    greeks:
      'Net long delta and short vega, so it gains from a rising market and from falling implied volatility together. Theta is positive and is the reason the position exists: every day the underlying does not fall, the short leg loses more time value than the long one. Gamma is negative near the short strike, and that is the danger — the loss accelerates exactly where the position is most likely to end up in a bad scenario.',
    whenToUse:
      'When implied volatility is elevated and you believe a level will hold. Selling premium is paid for taking the other side of fear, so the structure is most attractive after a sell-off has already inflated put prices, not in a quiet market where the credit is thin and the risk unchanged.',
    risks: [
      'The risk/reward is deliberately lopsided. A typical spread risks several dollars to make one, so a single loss undoes many wins and position sizing matters more than the win rate.',
      'Negative gamma near expiry. In the last week a small move through the short strike changes the position value far faster than the credit collected would suggest.',
      'Assignment on the short put leaves you long stock at the strike, requiring the capital to hold it or an immediate exit at whatever the market opens at.',
    ],
    example: {
      setup: 'SPY at 580. Sell the 570 put and buy the 560 put, 30 days out, for a net 2.80 credit.',
      rows: [
        ['Net credit received', '$2.80 per share — $280 for one contract'],
        ['Width of the spread', '$10.00 (570 − 560)'],
        ['Maximum profit', '$280, at or above 570'],
        ['Maximum loss', '$720 ((10 − 2.80) × 100), at or below 560'],
        ['Breakeven at expiry', '$567.20 (570 − 2.80)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'How much margin does this require?',
        a: 'Brokers normally require the width of the spread less the credit received — the maximum loss — held as buying power. In the example above that is $720 per contract, and the return on that capital, not on the credit, is the honest measure of the trade.',
      },
      {
        q: 'When should I close it?',
        a: 'Many traders close at a set fraction of the credit, commonly half to three-quarters, rather than holding to expiry. The remaining premium is the smallest part of the reward and it is collected during the window where gamma risk is highest.',
      },
    ],
  },

  'bear-call-spread': {
    slug: 'bear-call-spread',
    name: 'Bear Call Spread',
    outlook: 'Bearish to neutral — income',
    netCost: 'Credit',
    lede:
      'Sell a call and buy a higher-strike call above it. You keep the credit as long as the underlying stays below the short strike, and the long call bounds the loss if it does not. ' +
      'It is the credit-financed bearish counterpart to the bull put spread, and it is what turns an unbounded short-call risk into a number you can size.',
    construction: [
      'Sell 1 call at the lower strike K₁',
      'Buy 1 call at the higher strike K₂, same expiry',
    ],
    maxProfit: 'The net credit received. Kept in full at or below the short strike at expiry.',
    maxLoss: '(K₂ − K₁) − net credit. Reached at or above the long strike at expiry.',
    breakeven: 'K₁ + net credit, at expiry.',
    greeks:
      'Net short delta and short vega. Positive theta, which again is the point. Negative gamma above the short strike, and the asymmetry is worth naming: upside gaps in individual equities — a takeover bid, a surprise result — are the classic way a modest credit becomes the full width of the spread overnight, with no opportunity to manage in between.',
    whenToUse:
      'When you expect a level to cap the underlying and implied volatility is rich enough to pay for the risk. It sits naturally above resistance or above a strike you consider unreachable in the time remaining. Because upside call skew is usually flatter than downside put skew in equity indices, the credit here tends to be thinner than the mirror-image put spread — compare both before choosing.',
    risks: [
      'Takeover and gap risk. This is the structure most exposed to a single overnight headline, because the loss is realised in the direction that news arrives fastest.',
      'Early assignment before an ex-dividend date. A short in-the-money call is frequently assigned by holders capturing the dividend, leaving you short stock and liable for it.',
      'Negative gamma into expiry, where the position moves against you faster than the remaining credit compensates.',
    ],
    example: {
      setup: 'NVDA at 175. Sell the 185 call and buy the 195 call, 30 days out, for a net 2.35 credit.',
      rows: [
        ['Net credit received', '$2.35 per share — $235 for one contract'],
        ['Width of the spread', '$10.00 (195 − 185)'],
        ['Maximum profit', '$235, at or below 185'],
        ['Maximum loss', '$765 ((10 − 2.35) × 100), at or above 195'],
        ['Breakeven at expiry', '$187.35 (185 + 2.35)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'What if I am assigned on the short call?',
        a: 'You end up short 100 shares per contract at the strike. The long call still caps the loss above it, but you now carry a stock position with borrow costs and dividend liability. Exercising the long call closes it out; so does buying the shares back.',
      },
    ],
  },

  /* ============================== Volatility ============================= */

  straddle: {
    slug: 'straddle',
    name: 'Long Straddle',
    outlook: 'Volatility — direction-agnostic',
    netCost: 'Debit',
    lede:
      'Buy a call and a put at the same strike and expiry. The position is delta-neutral at inception and profits from a large move in either direction, which makes it the cleanest expression of "something is going to happen and I do not know which way". ' +
      'It is also the most expensive, because you are paying full time value twice and only one of the two legs can ever finish in the money.',
    construction: [
      'Buy 1 call at strike K',
      'Buy 1 put at the same strike K, same expiry',
    ],
    maxProfit:
      'Unlimited to the upside; bounded below by K − total debit if the underlying goes to zero.',
    maxLoss:
      'The total debit, realised only if the underlying finishes exactly at the strike. It is the single worst outcome and it is also the most likely single point.',
    breakeven: 'Two of them: K + total debit and K − total debit, at expiry.',
    greeks:
      'Delta near zero at the money, and it is the gamma that carries the position — the delta turns positive as the underlying rises and negative as it falls, so the position gets longer into strength and shorter into weakness on its own. Strongly long vega. Strongly short theta, and the decay is the worst on the board because two at-the-money options are decaying at once, which is why a straddle held through a quiet week is expensive even with the strike unchanged.',
    whenToUse:
      'Before a scheduled event whose outcome is binary but whose direction is genuinely unknown, or when realised volatility looks likely to exceed what implied volatility is charging. The second framing is the more durable one: a straddle is a bet that the market has underpriced movement, not merely that movement will occur.',
    risks: [
      'Implied volatility crush is the standard way this loses. Buying a straddle into earnings means paying an event premium that disappears the moment the result is out, so the underlying can gap hard and the position still lose.',
      'The move must exceed the combined premium, which is roughly twice the at-the-money price. That is a much larger threshold than people estimate before pricing it.',
      'Double decay. Two at-the-money options bleed together, and the loss is fastest in the final weeks.',
    ],
    example: {
      setup: 'TSLA at 340. Buy the 340 call at 18.50 and the 340 put at 16.20, 35 days out.',
      rows: [
        ['Total debit paid', '$34.70 per share — $3,470 for one contract pair'],
        ['Upper breakeven', '$374.70 (340 + 34.70)'],
        ['Lower breakeven', '$305.30 (340 − 34.70)'],
        ['Maximum loss', '$3,470, if TSLA finishes exactly at 340'],
        ['Profit if TSLA finishes at 420', '$4,530 ((420 − 340 − 34.70) × 100)'],
      ],
      note: FEES + ' Note how wide the breakevens are: TSLA must move about 10% either way just to return the premium.',
    },
    faqs: [
      {
        q: 'Why did my straddle lose money after a big earnings move?',
        a: 'Because the move was smaller than the one implied volatility had priced in. The straddle\'s cost embeds the market\'s expected move; beating the direction is not enough, you have to beat the size the market already charged you for.',
      },
      {
        q: 'Is a strangle better?',
        a: 'It is cheaper and needs a larger move. A strangle uses out-of-the-money strikes, so it costs less and has wider breakevens. Price both on the same expiry here and compare the breakevens against the move you expect.',
      },
    ],
  },

  strangle: {
    slug: 'strangle',
    name: 'Long Strangle',
    outlook: 'Volatility — direction-agnostic',
    netCost: 'Debit',
    lede:
      'A straddle built from out-of-the-money strikes: buy a call above the market and a put below it. The lower premium is the attraction and the wider breakevens are the cost, so it needs a bigger move to pay but risks less capital to find out. ' +
      'For the same money you can hold more strangles than straddles, which is the usual reason to prefer it.',
    construction: [
      'Buy 1 out-of-the-money call at strike K₂',
      'Buy 1 out-of-the-money put at strike K₁, K₁ < K₂, same expiry',
    ],
    maxProfit: 'Unlimited above; bounded below by K₁ − total debit.',
    maxLoss:
      'The total debit. Realised across the whole range between the two strikes, not at a single point as in a straddle.',
    breakeven: 'K₂ + total debit above, and K₁ − total debit below, at expiry.',
    greeks:
      'Delta near zero if the strikes are chosen symmetrically, though on equity indices they rarely are — put skew means an equidistant put costs more than the call, so a strangle placed by strike distance is usually short delta by construction. Long gamma and long vega, but both are lower than a straddle\'s because out-of-the-money options carry less of each. Short theta, though less per day than a straddle.',
    whenToUse:
      'When you expect a large move and want to pay less for the privilege, or when a straddle\'s premium is simply too large relative to the account. The wider the strikes, the more this becomes a bet on a tail rather than on movement generally.',
    risks: [
      'The dead zone is wide. Any finish between the two strikes loses the entire premium, and that region covers most realistic outcomes.',
      'Both legs decay simultaneously and neither has intrinsic value to defend it, so a quiet fortnight is very costly.',
      'Choosing strikes by equal distance rather than equal delta leaves an unintended directional bias on any underlying with skew.',
    ],
    example: {
      setup: 'QQQ at 500. Buy the 520 call at 6.10 and the 480 put at 7.40, 45 days out.',
      rows: [
        ['Total debit paid', '$13.50 per share — $1,350 for one contract pair'],
        ['Upper breakeven', '$533.50 (520 + 13.50)'],
        ['Lower breakeven', '$466.50 (480 − 13.50)'],
        ['Maximum loss', '$1,350, anywhere between 480 and 520'],
        ['Profit if QQQ finishes at 560', '$2,650 ((560 − 520 − 13.50) × 100)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'How do I pick the strikes?',
        a: 'By delta rather than by distance. Matching the call and put deltas — 20-delta against 20-delta, say — gives a position that is genuinely direction-neutral, which equal strike distances will not on a skewed underlying.',
      },
    ],
  },

  /* ========================== Defined-risk neutral ======================== */

  'iron-condor': {
    slug: 'iron-condor',
    name: 'Iron Condor',
    outlook: 'Neutral — range-bound',
    netCost: 'Credit',
    lede:
      'A bull put spread and a bear call spread sold together on the same expiry, one below the market and one above. You collect both credits and keep them if the underlying finishes between the two short strikes. ' +
      'It is the canonical range trade: four legs, defined risk on both sides, and a profit that depends on nothing happening.',
    construction: [
      'Sell 1 put at K₂ and buy 1 put at K₁ (K₁ < K₂), below the market',
      'Sell 1 call at K₃ and buy 1 call at K₄ (K₄ > K₃), above the market',
      'All four legs share an expiry',
    ],
    maxProfit:
      'The total net credit. Kept in full anywhere between the two short strikes at expiry.',
    maxLoss:
      'The width of the wider wing, less the total credit. Only one side can finish in the money, so the two wings never lose together.',
    breakeven: 'Two: K₂ − total credit below, and K₃ + total credit above.',
    greeks:
      'Delta near zero when centred, short vega, positive theta. The structural feature to understand is negative gamma at both short strikes: the position is calm in the middle and becomes violent at either edge, so risk is not distributed evenly across the range. Vega is the other live exposure — a general rise in implied volatility hurts even with the underlying pinned in the middle, because both short legs reprice upward at once.',
    whenToUse:
      'When implied volatility is high relative to what you expect to be realised, and you can name a range you believe will hold. Selling four legs into a quiet market with thin premium takes the same risk for a fraction of the reward, so the volatility level matters more than the directional view.',
    risks: [
      'The reward is small and the risk is large by design. Collecting two dollars to risk eight means a handful of losses erases many wins, so this only works with consistent sizing and an exit rule decided in advance.',
      'Both wings reprice on a volatility spike. A market-wide shock widens the whole surface, and the position can show a substantial loss while the underlying is still comfortably inside the range.',
      'Four legs, four spreads to cross twice. Execution friction is the largest fixed cost in this structure and it scales with how narrow the wings are.',
      'Pin risk at expiry, where a short strike finishing near the money leaves an uncertain assignment you find out about after the close.',
    ],
    example: {
      setup:
        'SPY at 580, 40 days out. Sell the 555 put / buy the 545 put for 1.55, and sell the 605 call / buy the 615 call for 1.35.',
      rows: [
        ['Total credit received', '$2.90 per share — $290 for one condor'],
        ['Width of each wing', '$10.00'],
        ['Maximum profit', '$290, anywhere between 555 and 605'],
        ['Maximum loss', '$710 ((10 − 2.90) × 100), at or beyond either long strike'],
        ['Lower breakeven', '$552.10 (555 − 2.90)'],
        ['Upper breakeven', '$607.90 (605 + 2.90)'],
      ],
      note: FEES + ' With four legs the round-trip spread cost is a material fraction of the $290 collected.',
    },
    faqs: [
      {
        q: 'How wide should the short strikes be?',
        a: 'Wider strikes raise the probability of keeping the credit and lower the credit itself; the expected value moves far less than either. Choose the range from where you genuinely think the underlying trades, then check whether the credit pays enough for the risk of being wrong.',
      },
      {
        q: 'Can both sides lose?',
        a: 'No. The underlying can only finish on one side of the range, so at most one wing is in the money at expiry. That is why the maximum loss is one wing\'s width rather than two.',
      },
      {
        q: 'When should I adjust?',
        a: 'Decide before entering. The common approaches are rolling the untested side closer to collect more credit, or closing the whole position at a set multiple of the credit received. Adjusting improvised under pressure usually adds risk to a position that is already losing.',
      },
    ],
  },

  'iron-butterfly': {
    slug: 'iron-butterfly',
    name: 'Iron Butterfly',
    outlook: 'Neutral — pinned',
    netCost: 'Credit',
    lede:
      'An iron condor with the two short strikes collapsed onto the same price: sell an at-the-money straddle and buy a protective wing on each side. The credit is far larger than a condor\'s and the profitable range far narrower. ' +
      'It is a bet not merely that the underlying stays in a range but that it finishes close to a specific number.',
    construction: [
      'Sell 1 call and 1 put at the central strike K',
      'Buy 1 put at K − w and 1 call at K + w for protection',
      'All four legs share an expiry',
    ],
    maxProfit:
      'The net credit, and only if the underlying finishes exactly at the central strike.',
    maxLoss: 'The wing width w, less the net credit.',
    breakeven: 'K ± net credit, at expiry.',
    greeks:
      'Short vega and positive theta, both larger in magnitude than an iron condor at comparable width because at-the-money options carry the most of each. Gamma is sharply negative right at the centre — the position is at its most profitable and its most unstable at the same price, which is the tension that defines it.',
    whenToUse:
      'When you expect a specific level to hold and implied volatility to fall, typically after an event has passed and the surface is still elevated. It pays roughly twice what a comparable condor pays and demands a correspondingly precise view.',
    risks: [
      'The profitable range is narrow, usually only the credit either side of the strike, so it is wrong far more often than a condor.',
      'Maximum profit requires an exact finish and is essentially never realised in full. Judge the trade by its value partway, not by the peak of the diagram.',
      'Pin risk at the central strike is acute: with a short call and a short put at the same price, an ambiguous close leaves genuine uncertainty about the resulting stock position.',
    ],
    example: {
      setup: 'SPY at 580, 30 days out. Sell the 580 straddle, buy the 560 put and the 600 call, for a net 9.20 credit.',
      rows: [
        ['Net credit received', '$9.20 per share — $920 for one butterfly'],
        ['Wing width', '$20.00'],
        ['Maximum profit', '$920, only at exactly 580'],
        ['Maximum loss', '$1,080 ((20 − 9.20) × 100), at or beyond 560 or 600'],
        ['Breakevens', '$570.80 and $589.20'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Iron butterfly or iron condor?',
        a: 'The butterfly collects more and wins less often; the condor collects less and wins more often. Neither is structurally better — they sit at different points on the same trade-off, and the choice should follow how confident you are about the level rather than the range.',
      },
    ],
  },

  butterfly: {
    slug: 'butterfly',
    name: 'Call Butterfly',
    outlook: 'Neutral — pinned',
    netCost: 'Debit',
    lede:
      'Buy one call below, sell two at the middle, buy one above, all equally spaced. It costs a small debit and pays its maximum if the underlying finishes exactly at the middle strike. ' +
      'The appeal is the ratio: a butterfly frequently risks one dollar to make four or five, which buys a very cheap way to express a precise view about where something settles.',
    construction: [
      'Buy 1 call at K₁',
      'Sell 2 calls at K₂',
      'Buy 1 call at K₃, with K₂ − K₁ = K₃ − K₂, same expiry',
    ],
    maxProfit: '(K₂ − K₁) − net debit, at exactly K₂ at expiry.',
    maxLoss: 'The net debit paid, at or beyond either outer strike.',
    breakeven: 'K₁ + net debit below, and K₃ − net debit above.',
    greeks:
      'Near-zero delta when centred on the market. Short vega and positive theta, and both grow sharply as expiry nears — a butterfly is worth very little until the last week or two, then converges quickly toward its terminal payoff. That late convergence is the practical fact about trading it: entering early costs little and does little, and most of the value appears at the end.',
    whenToUse:
      'When you have a specific price target and expiry in mind and want the cheapest structure that pays for being exactly right. It is also a common way to express a view about where an index pins into a monthly expiry, where large open interest at a strike can be self-reinforcing.',
    risks: [
      'It is rarely worth its maximum. The peak requires an exact finish, and realistic outcomes pay a fraction of it.',
      'Three strikes and four contracts make it fiddly to execute; legging in on a moving market often costs more than the edge.',
      'The position does very little until close to expiry, so it demands patience and is easy to abandon early at a small loss.',
    ],
    example: {
      setup: 'SPY at 580, 21 days out. Buy the 570 call, sell two 580 calls, buy the 590 call, for a net 2.10 debit.',
      rows: [
        ['Net debit paid', '$2.10 per share — $210 for one butterfly'],
        ['Wing width', '$10.00'],
        ['Maximum profit', '$790 ((10 − 2.10) × 100), only at exactly 580'],
        ['Maximum loss', '$210, at or beyond 570 or 590'],
        ['Breakevens', '$572.10 and $587.90'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Why use calls rather than puts?',
        a: 'At the same strikes the two are equivalent by put-call parity and should price identically. In practice pick whichever side is out of the money, because out-of-the-money options are more liquid and the spreads are tighter.',
      },
    ],
  },

  condor: {
    slug: 'condor',
    name: 'Call Condor',
    outlook: 'Neutral — range-bound',
    netCost: 'Debit',
    lede:
      'A butterfly with its peak stretched into a plateau: buy a low call, sell two calls at separated middle strikes, buy a high call. It pays its maximum across a range rather than at a point, which makes it forgiving where a butterfly is exact. ' +
      'Built entirely from calls, it is the debit-financed equivalent of an iron condor at the same strikes.',
    construction: [
      'Buy 1 call at K₁',
      'Sell 1 call at K₂ and 1 call at K₃',
      'Buy 1 call at K₄, with K₁ < K₂ < K₃ < K₄, same expiry',
    ],
    maxProfit: '(K₂ − K₁) − net debit, held anywhere between K₂ and K₃ at expiry.',
    maxLoss: 'The net debit paid, at or beyond K₁ or K₄.',
    breakeven: 'K₁ + net debit below, and K₄ − net debit above.',
    greeks:
      'Delta near zero when centred, short vega, positive theta. Like the butterfly it converges late, and like the iron condor it has two zones of negative gamma at the inner strikes rather than one at a single centre.',
    whenToUse:
      'When you want a butterfly\'s risk profile with a wider target zone and you would rather pay a debit than manage the assignment risk of short in-the-money legs. Compare it directly against an iron condor at the same four strikes — by parity they are near-equivalent, so choose on liquidity and on which legs would be in the money.',
    risks: [
      'Four legs and four strikes: the most execution-sensitive structure on this list.',
      'The maximum profit is capped and the plateau, while wider than a butterfly\'s peak, is still bounded on both sides.',
      'Slow to develop. Most of the value arrives in the final fortnight.',
    ],
    example: {
      setup: 'SPY at 580, 35 days out. Buy the 560 call, sell the 575 and 585 calls, buy the 600 call, for a net 4.40 debit.',
      rows: [
        ['Net debit paid', '$4.40 per share — $440 for one condor'],
        ['Inner width', '$15.00 (575 − 560)'],
        ['Maximum profit', '$1,060 ((15 − 4.40) × 100), between 575 and 585'],
        ['Maximum loss', '$440, at or beyond 560 or 600'],
        ['Breakevens', '$564.40 and $595.60'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'How does this differ from an iron condor?',
        a: 'Mostly in financing. The all-call condor is opened for a debit and the iron condor for a credit, but at the same strikes and expiry put-call parity makes the payoffs essentially identical. Choose on which legs are in the money and where the tighter markets are.',
      },
    ],
  },

  'jade-lizard': {
    slug: 'jade-lizard',
    name: 'Jade Lizard',
    outlook: 'Neutral to bullish — income',
    netCost: 'Credit',
    lede:
      'Sell an out-of-the-money put and an out-of-the-money call spread above the market. Its defining property is a construction rule rather than a shape: if the total credit collected is at least the width of the call spread, the position has no upside risk at all. ' +
      'That leaves a single risk — the underlying falling — which is precisely the risk somebody willing to own the stock is already comfortable with.',
    construction: [
      'Sell 1 out-of-the-money put at K_p',
      'Sell 1 call at K_c and buy 1 call at K_c + w above the market',
      'Choose strikes so the total credit ≥ w',
    ],
    maxProfit:
      'The total credit, kept if the underlying finishes between the short put and the short call.',
    maxLoss:
      'K_p − total credit on the downside, approached as the underlying falls toward zero. On the upside the loss is zero provided the credit rule holds.',
    breakeven: 'K_p − total credit on the downside. There is no upside breakeven when credit ≥ w.',
    greeks:
      'Net long delta from the short put, short vega, positive theta. The asymmetry is the whole design: the call spread is financed to be risk-free at expiry, so all of the position\'s exposure is concentrated below the market where the trader has deliberately chosen to take it.',
    whenToUse:
      'On an underlying you would be content to own at the short put strike, when implied volatility is high enough that the credit covers the call spread width. It is a way to be paid more than a cash-secured put pays without adding upside risk to a position you may want to keep.',
    risks: [
      'The credit rule is a constraint you must verify, not a property that comes free. If the total credit is less than the call spread width, the upside risk is real and the structure has lost its point.',
      'Downside risk is large and only nominally capped — a fall to zero costs the strike less the credit, exactly like a cash-secured put.',
      'Assignment on the short put leaves you long stock, so the capital to hold it has to be there.',
    ],
    example: {
      setup:
        'NVDA at 175, 35 days out. Sell the 160 put for 3.10, sell the 190 call for 2.45, buy the 195 call for 1.30.',
      rows: [
        ['Total credit received', '$4.25 per share — $425'],
        ['Call spread width', '$5.00 (195 − 190)'],
        ['Credit rule', '4.25 < 5.00 — NOT satisfied, so $75 of upside risk remains'],
        ['Maximum profit', '$425, between 160 and 190'],
        ['Downside breakeven', '$155.75 (160 − 4.25)'],
      ],
      note:
        FEES +
        ' This example deliberately fails the credit rule to show what that looks like: widen the put strike or narrow the call spread until the credit covers the width.',
    },
    faqs: [
      {
        q: 'What makes it "no upside risk"?',
        a: 'If the underlying finishes above the long call, the call spread loses exactly its width. When the credit collected is at least that width, the loss is fully paid for in advance and the worst upside outcome is breakeven or better.',
      },
    ],
  },

  /* =========================== Stock combinations ======================== */

  'covered-call': {
    slug: 'covered-call',
    name: 'Covered Call',
    outlook: 'Neutral to mildly bullish — income',
    netCost: 'Debit or credit',
    lede:
      'Own 100 shares and sell a call against them. The premium is income and a small cushion against a fall; the cost is that you have agreed to sell your shares at the strike, so everything above it belongs to the buyer. ' +
      'It is the most widely used options strategy and the most widely misunderstood: its risk is not the call, it is the stock you still own underneath.',
    construction: [
      'Hold 100 shares of the underlying',
      'Sell 1 call at strike K against them',
    ],
    maxProfit: '(K − cost basis) + premium received, if assigned at or above the strike.',
    maxLoss:
      'Cost basis − premium received, if the shares fall to zero. Substantial, and it is the stock\'s risk, not the option\'s.',
    breakeven: 'Cost basis − premium received.',
    greeks:
      'Net delta between 0 and 1 — long stock at 1 less the call\'s delta — so it is always less bullish than the shares alone. Short vega and positive theta from the call. Its payoff is identical to a short cash-secured put at the same strike, which is worth knowing because the two are usually discussed as though one were conservative and the other aggressive.',
    whenToUse:
      'On a holding you are willing to sell at the strike, in a market you expect to be flat or mildly higher. Selling calls on a position you intend to keep indefinitely eventually means either surrendering it in a rally or buying the call back at a loss to retain it.',
    risks: [
      'The upside is capped and the downside is not. A single strong rally forfeits the gain that would have justified holding the stock at all.',
      'Assignment risk rises sharply before an ex-dividend date when the call is in the money, and the dividend is often the reason it is exercised early.',
      'It is not a hedge. The premium offsets a small fall and does nothing against a large one.',
    ],
    example: {
      setup: 'Own 100 AAPL at a cost basis of 220, now trading 230. Sell the 240 call, 30 days out, for 4.10.',
      rows: [
        ['Premium received', '$4.10 per share — $410'],
        ['Breakeven', '$215.90 (220 − 4.10)'],
        ['Maximum profit if assigned at 240', '$2,410 ((240 − 220 + 4.10) × 100)'],
        ['Return if unassigned and flat', '$410 on the premium alone'],
        ['Loss if AAPL falls to 190', '$2,590 ((220 − 190 − 4.10) × 100)'],
      ],
      note: FEES + ' Tax treatment of assignment varies by jurisdiction and holding period.',
    },
    faqs: [
      {
        q: 'What if the stock rises far above the strike?',
        a: 'You are assigned and sell at the strike, keeping the premium. The gain is capped at the maximum above. Buying the call back to keep the shares is possible but costs more than the premium you received, turning a profitable trade into a loss on the option leg.',
      },
      {
        q: 'Which strike gives the best income?',
        a: 'Lower strikes pay more premium and cap the upside sooner; higher strikes pay less and leave more room. There is no free lunch in the choice — the premium is compensation for exactly the upside you surrender.',
      },
    ],
  },

  'cash-secured-put': {
    slug: 'cash-secured-put',
    name: 'Cash-Secured Put',
    outlook: 'Neutral to bullish — income or acquisition',
    netCost: 'Credit',
    lede:
      'Sell a put and hold enough cash to buy the shares if assigned. Either the option expires and you keep the premium, or you are assigned and buy the stock at a net cost below the strike. ' +
      'Both outcomes are acceptable if — and only if — you genuinely want to own the underlying at that price.',
    construction: [
      'Sell 1 put at strike K',
      'Set aside K × 100 in cash per contract to cover assignment',
    ],
    maxProfit: 'The premium received, kept in full at or above the strike at expiry.',
    maxLoss: 'K − premium, if the underlying falls to zero.',
    breakeven: 'K − premium received.',
    greeks:
      'Long delta, short vega, positive theta. Its payoff is identical to a covered call at the same strike, so the two are the same trade in different clothes; the cash-secured put simply expresses it without owning the shares first.',
    whenToUse:
      'When you want to buy the underlying below the market and are content to be paid for waiting. Elevated implied volatility raises the premium, which is why this works best after a sell-off — the same conditions that make people want to sell are what make the put worth selling.',
    risks: [
      'The premium is a thin cushion. A 3% credit against a 30% fall leaves you holding a badly damaged position.',
      'The cash is committed for the life of the option, so the return on capital is far lower than the return on premium suggests.',
      'Selling puts on a stock you do not actually want exposes you to acquiring something you must then immediately sell at a loss.',
    ],
    example: {
      setup: 'SPY at 580. Sell the 560 put, 30 days out, for 5.60. Set aside $56,000.',
      rows: [
        ['Premium received', '$5.60 per share — $560'],
        ['Cash secured', '$56,000 per contract'],
        ['Maximum profit', '$560 (1.0% on the cash committed over 30 days)'],
        ['Effective purchase price if assigned', '$554.40 (560 − 5.60)'],
        ['Loss if SPY falls to 500', '$5,440 ((560 − 500 − 5.60) × 100)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Is this safer than buying the stock outright?',
        a: 'Slightly, and only by the premium. Below the strike you carry essentially the same downside as the shares, less the credit; above it you forgo the upside entirely. It is a different distribution, not a smaller risk.',
      },
    ],
  },

  'protective-put': {
    slug: 'protective-put',
    name: 'Protective Put',
    outlook: 'Bullish — hedged',
    netCost: 'Debit',
    lede:
      'Own the shares and buy a put against them. The put sets a floor: below its strike, further falls in the stock are matched by gains in the option. The upside stays fully intact, less the premium paid. ' +
      'It is portfolio insurance in the literal sense, including the part where you pay a premium and usually get nothing back.',
    construction: [
      'Hold 100 shares of the underlying',
      'Buy 1 put at strike K as protection',
    ],
    maxProfit: 'Unlimited. The stock\'s upside less the premium paid.',
    maxLoss: '(Cost basis − K) + premium paid. Fixed and known from the day it is placed.',
    breakeven: 'Cost basis + premium paid.',
    greeks:
      'Net delta between 0 and 1. Long vega, which is unusual for a stock holder and valuable: implied volatility rises when markets fall, so the hedge gains value from the panic as well as from the price. Short theta — the premium bleeds every day nothing happens, which is what insurance costs.',
    whenToUse:
      'When you must keep a position — for tax reasons, a lock-up, or conviction — through a period you consider dangerous. Its identical twin is a long call at the same strike, and comparing the two prices here is worthwhile: parity says they should match, and any gap is a financing or borrow cost worth understanding before choosing.',
    risks: [
      'It is expensive to maintain. Rolling protection continuously can consume much of a portfolio\'s long-run return.',
      'The floor sits at the strike, not at today\'s price. Everything between the current price and the strike is still your loss.',
      'Buying protection after a fall means buying it at the highest implied volatility of the cycle, which is when it is least worth the price.',
    ],
    example: {
      setup: 'Own 100 NVDA at a cost basis of 170, now 175. Buy the 165 put, 60 days out, for 6.80.',
      rows: [
        ['Premium paid', '$6.80 per share — $680'],
        ['Floor established', '$165 per share'],
        ['Maximum loss', '$1,180 ((170 − 165 + 6.80) × 100), whatever happens below 165'],
        ['Breakeven', '$176.80 (170 + 6.80)'],
        ['Profit if NVDA reaches 220', '$4,320 ((220 − 170 − 6.80) × 100)'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'Why not just sell the stock?',
        a: 'Selling realises a taxable gain, forfeits dividends and the upside, and requires deciding when to buy back. The put keeps all of that intact for a known fee. If none of those apply to you, selling is usually cheaper.',
      },
    ],
  },

  collar: {
    slug: 'collar',
    name: 'Collar',
    outlook: 'Neutral — hedged, bounded',
    netCost: 'Debit or credit',
    lede:
      'Own the shares, buy a protective put, and sell a call to pay for it. The put sets a floor and the call sets a ceiling, so the outcome is fenced on both sides and the financing is close to free. ' +
      'It is the standard structure for holding a concentrated position you cannot sell, and it works precisely because you give up the upside you were not counting on anyway.',
    construction: [
      'Hold 100 shares of the underlying',
      'Buy 1 put at K_p below the market',
      'Sell 1 call at K_c above the market, same expiry',
    ],
    maxProfit: '(K_c − cost basis) + net credit, or less the net debit.',
    maxLoss: '(Cost basis − K_p) + net debit, or less the net credit.',
    breakeven: 'Cost basis + net debit, or cost basis − net credit.',
    greeks:
      'Net delta well below 1 and falling as the underlying approaches the call strike. Vega is roughly neutral because the long put and short call offset, which is what makes a collar cheap to carry compared with a bare protective put. Theta is small and can sit either side of zero depending on the strikes.',
    whenToUse:
      'To hold a large or restricted position through a period of risk at near-zero cost — after a lock-up, ahead of a diversification you cannot yet execute, or around a binary event. A "zero-cost collar" simply means the strikes were chosen so the premiums cancel; it is not free, the price is the upside above the call.',
    risks: [
      'The upside is capped. On a position that then doubles, the collar is by far the most expensive decision in the account despite having cost nothing to place.',
      'Early assignment on the short call, especially around dividends, can force the sale you were trying to defer — sometimes with the tax consequence you built the collar to avoid.',
      'Constructive-sale and tax rules in some jurisdictions treat a tight collar as a disposal. Take advice before using one on a low-basis holding.',
    ],
    example: {
      setup: 'Own 100 SPY at a cost basis of 560, now 580. Buy the 560 put for 6.20, sell the 605 call for 6.05.',
      rows: [
        ['Net cost', '$0.15 per share — $15 debit, close to zero-cost'],
        ['Floor', '$560 per share'],
        ['Ceiling', '$605 per share'],
        ['Maximum profit', '$4,485 ((605 − 560) × 100 − 15)'],
        ['Maximum loss', '$15, the net debit, since the floor equals the cost basis'],
      ],
      note: FEES,
    },
    faqs: [
      {
        q: 'What is a zero-cost collar?',
        a: 'One where the call premium exactly funds the put, so no cash changes hands. The cost is real but paid in forgone upside rather than in cash, which is why the label is more flattering than the trade.',
      },
    ],
  },

  'risk-reversal': {
    slug: 'risk-reversal',
    name: 'Risk Reversal',
    outlook: 'Bullish — leveraged, undefined risk',
    netCost: 'Debit or credit',
    lede:
      'Sell an out-of-the-money put and use the proceeds to buy an out-of-the-money call. The result is a synthetic long position with a gap in the middle: nothing happens between the strikes, and outside them it behaves much like owning the underlying. ' +
      'It is cheap or free to enter, and that is exactly why it deserves care — the financing comes from selling the downside.',
    construction: [
      'Sell 1 out-of-the-money put at K_p',
      'Buy 1 out-of-the-money call at K_c, K_c > K_p, same expiry',
    ],
    maxProfit: 'Unlimited above the call strike.',
    maxLoss: 'K_p − net credit, approached as the underlying falls to zero. Substantial.',
    breakeven:
      'K_c − net credit if opened for a credit, or K_c + net debit if opened for a debit.',
    greeks:
      'Strongly long delta with a flat middle, and it is the vega that gives the structure its name: it is long the call\'s volatility and short the put\'s, so on an underlying with the usual equity skew it is short volatility on balance. In currency markets the price of this structure is quoted directly as the risk reversal and is the standard measure of skew.',
    whenToUse:
      'When you are firmly bullish, want leverage without paying net premium, and are genuinely prepared to own the underlying at the put strike. It is also the natural way to monetise steep put skew, since you are selling the expensive wing to buy the cheaper one.',
    risks: [
      'The downside is large and only bounded by zero. This is not a defined-risk position and should not be sized like one.',
      'Margin requirements on the naked short put are substantial and rise as the market falls, which is when the position is already losing.',
      'A zero-cost entry disguises the risk. Costing nothing to open says nothing about what it can cost to hold.',
    ],
    example: {
      setup: 'SPY at 580, 45 days out. Sell the 550 put for 6.30, buy the 610 call for 6.10.',
      rows: [
        ['Net credit received', '$0.20 per share — $20'],
        ['Upside participation begins', '$610'],
        ['Downside exposure begins', '$550'],
        ['Result between 550 and 610', '+$20, the credit, and nothing else'],
        ['Loss if SPY falls to 500', '$4,980 ((550 − 500) × 100 − 20)'],
      ],
      note: FEES + ' The short put requires margin well above the $20 credit received.',
    },
    faqs: [
      {
        q: 'How does this differ from just buying the stock?',
        a: 'Between the strikes it does nothing, so it needs a real move to pay. Outside them it behaves similarly but with far less capital committed, which means the leverage — and the margin call risk — is much higher.',
      },
    ],
  },

  /* ============================ Time spreads ============================= */

  'calendar-spread': {
    slug: 'calendar-spread',
    name: 'Calendar Spread',
    outlook: 'Neutral — long volatility of time',
    netCost: 'Debit',
    lede:
      'Sell a near-dated option and buy a longer-dated one at the same strike. The near leg decays faster than the far leg, and the difference is the profit. It is the one structure here whose payoff cannot be drawn as straight lines, because at the near expiry the far leg is still alive and must be valued by a model rather than by arithmetic. ' +
      'That is also why it is the only one on this list with no closed-form maximum profit.',
    construction: [
      'Sell 1 option at strike K expiring at T₁',
      'Buy 1 option at the same strike K expiring at T₂, T₂ > T₁',
    ],
    maxProfit:
      'No closed form. It occurs with the underlying at the strike on the near expiry, and its size depends on the far leg\'s implied volatility at that moment — which is unknown when you enter.',
    maxLoss: 'The net debit paid, approached if the underlying moves far from the strike either way.',
    breakeven:
      'Two, either side of the strike, and both are model-dependent rather than arithmetic. Use the payoff curve on this page rather than a formula.',
    greeks:
      'Long vega, because the far leg has more of it than the near leg — this position wants implied volatility to rise, which distinguishes it from every other neutral structure here. Positive theta while the underlying is near the strike. Negative gamma near the strike as the near expiry approaches. It is also exposed to the shape of the term structure, not merely its level: the two legs can reprice differently even with the underlying still.',
    whenToUse:
      'When you expect the underlying to sit near a level in the short term but volatility to rise later — a quiet stretch before a known event beyond the near expiry is the textbook case. Selling the front month into an event and owning the back month is how the structure is usually built around earnings.',
    risks: [
      'A large move either way loses the debit, and the position is at its most vulnerable to being wrong about calm rather than about direction.',
      'Term-structure risk: a fall in back-month implied volatility can produce a loss with the underlying exactly where you wanted it.',
      'Early assignment on the short leg leaves an unhedged position in the underlying against a long option, which is not the same risk you entered.',
      'The two legs have different expiries, so this cannot be closed as a single guaranteed unit at the near expiry.',
    ],
    example: {
      setup: 'SPY at 580. Sell the 580 call expiring in 14 days for 6.40, buy the 580 call expiring in 49 days for 11.20.',
      rows: [
        ['Net debit paid', '$4.80 per share — $480'],
        ['Maximum loss', '$480, if SPY moves far from 580 by the near expiry'],
        ['Best case', 'SPY at exactly 580 on day 14, with back-month volatility unchanged or higher'],
        ['Position after the near expiry', 'A long 49-day 580 call, if you let the short leg expire'],
      ],
      note: FEES + ' The profit at the near expiry depends on the back month\'s implied volatility and cannot be computed from the strikes alone.',
    },
    faqs: [
      {
        q: 'Why is there no maximum profit formula?',
        a: 'Because when the short leg expires the long leg still has time value, and time value is a model output, not an arithmetic identity. Every other structure here settles entirely at one expiry, which is what makes their payoffs solvable in closed form.',
      },
      {
        q: 'Calls or puts?',
        a: 'At the same strike they behave almost identically. Choose the out-of-the-money side for tighter markets and to reduce the chance of early assignment on the short leg.',
      },
    ],
  },

  'diagonal-spread': {
    slug: 'diagonal-spread',
    name: 'Diagonal Spread',
    outlook: 'Directional — with a time component',
    netCost: 'Debit',
    lede:
      'A calendar spread with the strikes moved apart: sell a near-dated option at one strike and buy a longer-dated one at another. It combines the time-decay harvesting of a calendar with the directional lean of a vertical, and the strike choice decides which of the two dominates. ' +
      'Used at wide strikes and long back-month expiries it becomes the "poor man\'s covered call", a stock replacement built entirely from options.',
    construction: [
      'Sell 1 option at strike K₁ expiring at T₁',
      'Buy 1 option at strike K₂ expiring at T₂, with K₁ ≠ K₂ and T₂ > T₁',
    ],
    maxProfit:
      'No closed form, for the same reason as the calendar: the back leg survives the front expiry and must be valued by a model.',
    maxLoss:
      'Bounded by the net debit when the long leg is more valuable than the short at every price — which is not automatic. Verify it with the payoff curve for your specific strikes rather than assuming it.',
    breakeven: 'Model-dependent. Read it off the curve on this page.',
    greeks:
      'Long vega and positive theta like a calendar, plus a directional delta from the strike offset. Because the two legs sit at different strikes on different expiries, they also sit at different points on the volatility surface, so the position is exposed to skew changes as well as to level and term structure.',
    whenToUse:
      'When you have a directional lean and want the short leg to fund it, or as a capital-efficient stock replacement: a deep in-the-money long-dated call with a near-dated out-of-the-money call sold against it behaves much like a covered call for a fraction of the capital.',
    risks: [
      'The loss is only bounded if the long leg dominates at every price. A poorly chosen pair can leave a gap where the short leg outruns the long one.',
      'Assignment on the short leg against a longer-dated long leg is not a closed position and can require capital at short notice.',
      'It carries level, term-structure and skew exposure at once, which makes attributing a loss harder than in any other structure here.',
    ],
    example: {
      setup:
        'SPY at 580. Sell the 595 call expiring in 21 days for 3.90, buy the 585 call expiring in 60 days for 12.60.',
      rows: [
        ['Net debit paid', '$8.70 per share — $870'],
        ['Directional lean', 'Bullish toward 595 by the near expiry'],
        ['Best case', 'SPY just below 595 on day 21'],
        ['Position after the near expiry', 'A long 60-day 585 call if the short leg expires worthless'],
      ],
      note: FEES + ' Check the payoff curve for your own strikes — the loss bound is not automatic in a diagonal.',
    },
    faqs: [
      {
        q: 'What is a "poor man\'s covered call"?',
        a: 'A diagonal where the long leg is a deep in-the-money, long-dated call standing in for the 100 shares, with a short-dated out-of-the-money call sold against it. It replicates a covered call for far less capital, and adds the risk that the long call decays where shares would not.',
      },
    ],
  },

  /* ================================ Futures ============================== */

  'futures-outright': {
    slug: 'futures-outright',
    name: 'Futures Outright',
    outlook: 'Directional — leveraged, linear',
    netCost: 'Margin',
    lede:
      'A single long or short futures contract. The payoff is linear and symmetric — there is no premium, no decay and no strike — and the entire character of the position comes from leverage: you post margin worth a fraction of the notional and gain or lose on the whole of it. ' +
      'One E-mini S&P contract at 5,800 controls $290,000 of index exposure on roughly $20,000 of margin.',
    construction: ['Buy (or sell) 1 futures contract for a given delivery month'],
    maxProfit:
      'Unbounded for a long as the price rises; for a short, bounded only by the price falling to zero.',
    maxLoss:
      'Bounded by the price reaching zero for a long, and unbounded for a short. In both cases losses can exceed the margin posted, and the balance is owed.',
    breakeven: 'The entry price, adjusted for commissions and any financing.',
    greeks:
      'Delta is 1 per contract and nothing else applies — no gamma, no vega, no theta. What replaces them is the multiplier and the margin cycle: profit and loss are marked to market and settled in cash daily, so an adverse move demands cash before the trade is over rather than at the end of it.',
    whenToUse:
      'For efficient directional exposure to an index, rate or commodity, for hedging an existing physical or portfolio position, or for round-the-clock access to a market whose cash session is closed. It is also the cleanest instrument when you want exposure without an options premium to recover.',
    risks: [
      'Losses are not limited to the margin posted. A gap through your stop can leave a debit balance owed to the broker.',
      'Daily variation margin is a cash obligation. A position that is ultimately right can be closed out by a margin call before it gets there.',
      'Contracts expire and must be rolled, and the roll has a cost set by the shape of the curve, not by your view.',
      'Multipliers vary widely between products — the loss per point on one contract is a fact to check before trading, not after.',
    ],
    example: {
      setup: 'Long 1 E-mini S&P 500 (ES) contract at 5,800. Multiplier $50 per index point.',
      rows: [
        ['Notional controlled', '$290,000 (5,800 × $50)'],
        ['Typical initial margin', 'Roughly $20,000, about 7% of notional'],
        ['Value of a 1-point move', '$50'],
        ['Profit if ES rises to 5,900', '$5,000 (100 points × $50)'],
        ['Loss if ES falls to 5,700', '$5,000, payable as variation margin'],
      ],
      note: FEES + ' Margin requirements are set by the exchange and the broker and change with volatility.',
    },
    faqs: [
      {
        q: 'What happens at expiry?',
        a: 'Depends on the contract. Financially settled products like the E-mini S&P settle to cash against a final index value; physically delivered ones like crude oil require delivery. Most participants close or roll well before that — check the first notice date, not just expiry.',
      },
      {
        q: 'Can I lose more than my margin?',
        a: 'Yes. Margin is a performance bond, not the maximum loss. A large gap can produce a loss exceeding the account balance, and the deficit is a debt.',
      },
    ],
  },

  'futures-spread': {
    slug: 'futures-spread',
    name: 'Futures Spread',
    outlook: 'Relative value — non-directional',
    netCost: 'Margin',
    lede:
      'Long one futures contract and short a related one, so the position profits from the difference between them rather than from the direction of either. Because the two legs share most of their risk, a shock that moves the whole market largely cancels. ' +
      'Exchanges recognise this and charge dramatically less margin for a recognised spread than for the two legs held separately.',
    construction: [
      'Buy 1 futures contract in one delivery month or product',
      'Sell 1 related futures contract, usually as a single exchange-recognised spread order',
    ],
    maxProfit:
      'Bounded by how far the differential can move, which is a question about the market\'s structure rather than a formula. Physical arbitrage limits often cap it in practice.',
    maxLoss:
      'Likewise bounded by the differential rather than by a strike. It is not defined risk, and a spread can move far further than its history suggests.',
    breakeven: 'The differential at entry, adjusted for costs.',
    greeks:
      'Delta-neutral to the shared underlying and fully exposed to the relationship between the legs. The risk that remains after the common factor cancels is the whole position, which is why spreads are quoted, traded and margined as one instrument rather than two.',
    whenToUse:
      'When you have a view on a relationship — one month against another, one grade against another, one location against another — and no view on the outright level. It is also the standard way to roll an expiring position forward without being flat in between.',
    risks: [
      'Leverage is much higher because margin is much lower. The reduced margin reflects reduced volatility, not reduced risk per dollar committed, and the two are easy to confuse.',
      'Spread relationships can break. A supply shock, a storage constraint or a delivery squeeze can move a differential further than any historical range.',
      'Legging in or out separately destroys the margin offset and briefly exposes the full outright risk of both legs.',
    ],
    example: {
      setup: 'Long 1 ES September contract at 5,800, short 1 ES December contract at 5,845.',
      rows: [
        ['Spread at entry', '−45 points (September minus December)'],
        ['Value of a 1-point change in the spread', '$50'],
        ['Profit if the spread narrows to −30', '$750 (15 points × $50)'],
        ['Loss if the spread widens to −60', '$750'],
        ['Margin', 'Far below the sum of the two outright requirements'],
      ],
      note: FEES + ' Enter as a single spread order to obtain the margin offset and avoid legging risk.',
    },
    faqs: [
      {
        q: 'Why is the margin so much lower?',
        a: 'Because the exchange recognises the offsetting risk between the legs. The common market factor largely cancels, so the residual volatility of the spread is a small fraction of either leg — and margin is sized to that residual.',
      },
    ],
  },

  'futures-calendar-spread': {
    slug: 'futures-calendar-spread',
    name: 'Futures Calendar Spread',
    outlook: 'Term structure — non-directional',
    netCost: 'Margin',
    lede:
      'Long one delivery month of a product and short another month of the same product. It isolates the shape of the forward curve — contango or backwardation — from its level, so it profits from the curve steepening or flattening while direction is largely irrelevant. ' +
      'It is the single most common futures spread, and every roll of a long-dated position is one whether the trader thinks of it that way or not.',
    construction: [
      'Buy 1 contract in the near (or far) month',
      'Sell 1 contract in the other month of the same product',
    ],
    maxProfit:
      'Bounded by how far the calendar differential can move. In storable commodities the carry cost — storage, insurance and financing — caps contango, because beyond it physical arbitrage becomes profitable.',
    maxLoss:
      'Bounded by the same differential in the other direction, and that direction has no equivalent cap: backwardation can widen without limit when physical supply is short.',
    breakeven: 'The differential at entry, adjusted for costs.',
    greeks:
      'Delta-neutral to the outright price, exposed to the slope of the curve. The asymmetry above is the thing to hold on to: contango is limited by arbitrage and backwardation is not, so the risk in a calendar spread is genuinely one-sided in most physical markets.',
    whenToUse:
      'To express a view on storage, carry or seasonal demand, or to roll an existing position from an expiring month into the next one. Term structure is also the cleanest read on physical tightness in commodities, which is why these spreads are watched as an indicator as much as traded.',
    risks: [
      'Squeezes. A short near-month leg in a physically delivered market during a supply shortage is the classic route to an unbounded loss.',
      'Seasonality that is well known is already in the price; trading the calendar on the pattern alone means paying for information everyone has.',
      'Delivery and first notice dates arrive before expiry, and holding a physically deliverable near leg past them creates an obligation, not a position.',
    ],
    example: {
      setup: 'Long 1 crude oil (CL) December at 78.40, short 1 crude oil June at 80.10. Multiplier $1,000 per dollar.',
      rows: [
        ['Spread at entry', '−1.70 (December minus June), a contango market'],
        ['Value of a $0.01 change in the spread', '$10'],
        ['Profit if the spread moves to −0.70', '$1,000'],
        ['Loss if the spread widens to −2.70', '$1,000'],
        ['Structural cap on contango', 'Storage plus financing plus insurance for the period'],
      ],
      note: FEES + ' Crude is physically delivered: check first notice dates before holding the near leg.',
    },
    faqs: [
      {
        q: 'What do contango and backwardation mean?',
        a: 'Contango is later months priced above nearer ones, the normal state for a storable commodity where carry costs money. Backwardation is the reverse, and it signals that having the physical goods now is worth paying for — usually a sign of scarcity.',
      },
    ],
  },

  'futures-intercommodity-spread': {
    slug: 'futures-intercommodity-spread',
    name: 'Futures Inter-Commodity Spread',
    outlook: 'Relative value — processing or substitution margin',
    netCost: 'Margin',
    lede:
      'Long one product and short a different but economically linked one — crude against its refined products, soybeans against oil and meal, corn against ethanol. The spread usually represents a real processing margin that somebody in the physical market earns or pays. ' +
      'That is what separates it from a statistical pairs trade: there is a plant somewhere whose economics enforce the relationship.',
    construction: [
      'Buy N contracts of the input product',
      'Sell M contracts of the output product, in the ratio the physical process actually uses',
    ],
    maxProfit:
      'Bounded by how far the processing margin can move. Sustained levels far above the cost of processing bring more capacity online, which pulls it back.',
    maxLoss:
      'Bounded by the margin moving the other way, and negative processing margins do occur — plants run at a loss or shut, and the spread can stay adverse for as long as that persists.',
    breakeven: 'The spread level at entry, adjusted for costs.',
    greeks:
      'Neutral to the common commodity factor, exposed to the relationship between the two products. The contract ratio is load-bearing: a 3:2:1 crack spread is three crude against two gasoline and one heating oil because that approximates a refinery\'s actual yield, and getting the ratio wrong leaves an unintended outright position.',
    whenToUse:
      'To trade refining, crushing or processing economics, or to hedge them if you are in that business. Seasonal demand — gasoline into summer, heating oil into winter — is the usual source of a view.',
    risks: [
      'Ratio error is the characteristic mistake here, and it converts a spread into a leveraged outright position without announcing itself.',
      'The legs are different products with different liquidity, delivery locations and contract months, so they can dislocate for reasons unrelated to the processing margin.',
      'Refinery outages, weather and policy changes move these spreads violently and without warning.',
    ],
    example: {
      setup: 'A 3:2:1 crack spread. Long 3 crude oil contracts, short 2 gasoline and 1 heating oil.',
      rows: [
        ['What it represents', 'A refinery\'s gross margin from processing three barrels of crude'],
        ['Ratio', '3 crude in, 2 gasoline and 1 heating oil out — an approximate real yield'],
        ['Typical quotation', 'Dollars per barrel of crude input'],
        ['Profit driver', 'Refined product prices rising faster than crude'],
        ['Seasonal pattern', 'Gasoline cracks usually firm into the summer driving season'],
      ],
      note:
        FEES +
        ' Note: this calculator prices the crack spread correctly, but the plain-English assistant was never trained on commodity roots and will not parse it — build the legs directly.',
    },
    faqs: [
      {
        q: 'Why 3:2:1?',
        a: 'It approximates the yield of a typical refinery: three barrels of crude produce roughly two of gasoline and one of distillate. The ratio is a rough industry convention, not a physical constant, and other ratios such as 5:3:2 are used where the yield differs.',
      },
    ],
  },

  'futures-basis-arbitrage': {
    slug: 'futures-basis-arbitrage',
    name: 'Futures Basis Trade',
    outlook: 'Arbitrage — carry capture',
    netCost: 'Margin',
    lede:
      'Buy the asset in the cash market and sell the futures contract against it, holding both to expiry so the basis converges. The profit is fixed at the outset: it is the difference between the futures price and the spot price, less the cost of carrying the asset until delivery. ' +
      'Also called cash and carry, it is the trade whose existence forces futures to price near fair value in the first place.',
    construction: [
      'Buy the underlying asset in the cash market',
      'Sell 1 futures contract against it',
      'Carry both to expiry, when the basis converges to zero by construction',
    ],
    maxProfit:
      'The basis captured at entry, less financing, storage and insurance. Known at the outset, which is what makes it an arbitrage rather than a trade.',
    maxLoss:
      'Small in principle, but real in practice. It comes from financing costs rising, the position being unwound early, or a failure of the convergence assumption — not from the price of the asset.',
    breakeven: 'Where the basis equals the total cost of carry.',
    greeks:
      'Delta-neutral by construction. The exposures that remain are financing rate, storage cost and the reliability of convergence — the risks of a balance sheet rather than of a price.',
    whenToUse:
      'When the futures price exceeds spot by more than it costs to carry the asset, and you have the funding, the storage and the operational capacity to hold both legs to delivery. In practice that combination restricts it largely to institutions.',
    risks: [
      'Funding risk. The trade is financed, and if financing costs rise after entry the locked-in margin shrinks or disappears.',
      'Mark-to-market on the futures leg requires variation margin daily while the cash leg produces no offsetting cash, so a rally creates a funding demand on a position that is fully hedged.',
      'It is heavily leveraged by construction, so a small basis move against a large position matters. This is the mechanism behind more than one well-documented dislocation in Treasury markets.',
      'Storage, insurance and delivery logistics are real costs and real operational risks in physical commodities.',
    ],
    example: {
      setup: 'Gold spot at 2,650. The six-month future trades at 2,704. Financing costs 3.6% annualised.',
      rows: [
        ['Basis', '$54 (2,704 − 2,650)'],
        ['Cost of carry for six months', 'About $48 (2,650 × 3.6% × 0.5), before storage'],
        ['Gross arbitrage profit', 'About $6 per ounce, before storage and insurance'],
        ['Risk to the price of gold', 'None — the legs offset'],
        ['Real risk', 'Financing rising, or variation margin on the short future'],
      ],
      note: FEES + ' Storage and insurance for physical metal are excluded and can consume the entire $6 margin.',
    },
    faqs: [
      {
        q: 'Is this genuinely risk-free?',
        a: 'No. The price risk is hedged, but funding, margin and operational risks are not. The 2020 Treasury basis episode is the standard illustration: a hedged, leveraged position forced to unwind by margin demands rather than by any view on direction.',
      },
    ],
  },

  'covered-futures-call': {
    slug: 'covered-futures-call',
    name: 'Covered Futures Call',
    outlook: 'Neutral to mildly bullish — income on a futures position',
    netCost: 'Margin',
    lede:
      'Hold a long futures contract and sell a call option on that same future against it. The economics mirror an equity covered call — premium in exchange for capped upside — with one difference that changes the risk entirely: the underlying is a leveraged, margined contract rather than fully paid shares. ' +
      'The premium cushions a fall; the leverage beneath it does not go away.',
    construction: [
      'Hold 1 long futures contract',
      'Sell 1 call option on that future (an FOP) at strike K',
    ],
    maxProfit: '(K − futures entry price) × multiplier + premium received, if assigned.',
    maxLoss:
      'Futures entry price × multiplier, less the premium, if the future falls to zero — and unlike an equity covered call, the loss can exceed the margin posted.',
    breakeven: 'Futures entry price − premium received, in points.',
    greeks:
      'Net delta below 1 and falling toward zero as the future approaches the strike. Short vega and positive theta from the option. The critical structural point: the premium received does not reduce the margin obligation on the futures leg, so it cushions the loss without cushioning the cash calls that arrive first.',
    whenToUse:
      'To generate income on a futures position you expect to be flat or drift higher, or to set an exit level and be paid for committing to it. On commodities it is a common way to monetise a range in a market you must hold for hedging reasons.',
    risks: [
      'The leverage is the risk, not the option. A fall large enough to exhaust the margin produces a cash call regardless of the premium collected.',
      'Options on futures may be American-style and can be assigned early, leaving you flat when you meant to be long.',
      'Option expiry and futures expiry are often different dates. Check both — a covered position can become naked days before you expect.',
      'The premium is small relative to the notional, so it offsets only a modest move.',
    ],
    example: {
      setup: 'Long 1 ES future at 5,800. Sell the 5,900 call, 30 days out, for 42.00 points. Multiplier $50.',
      rows: [
        ['Premium received', '$2,100 (42.00 × $50)'],
        ['Breakeven on the future', '5,758 points'],
        ['Maximum profit if assigned at 5,900', '$7,100 ((100 + 42) × $50)'],
        ['Result if ES is flat at 5,800', '$2,100, the premium'],
        ['Loss if ES falls to 5,600', '$7,900 ((200 − 42) × $50), payable as margin'],
      ],
      note: FEES + ' Verify that the option expiry and the futures expiry are the ones you intend.',
    },
    faqs: [
      {
        q: 'How is this different from an equity covered call?',
        a: 'The payoff shape is the same; the funding is not. Equity shares are paid for in full, so the worst case is the capital you committed. A futures contract is margined, so the loss can exceed what you posted and arrives as a daily cash demand.',
      },
    ],
  },
};

/** Slugs that have a guide, for the homepage index and for build-time checks. */
export const GUIDED_SLUGS = Object.keys(STRATEGY_GUIDES);

export function getStrategyGuide(slug: string): StrategyGuide | undefined {
  return STRATEGY_GUIDES[slug];
}
