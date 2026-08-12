import { create } from 'zustand';
import { createClient } from '../lib/supabase/client';
import { authMetadata } from '../lib/licence';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { StrategyRequest, Leg as ProtoLeg, QuoteRequest, ChainRequest, RiskFreeRateRequest } from '../grpc/calculator_pb';

/**
 * The averaging vocabulary, borrowed rather than restated.
 *
 * `useTreePricerStore` already owns `AsianStyle` for the Exercise & Averaging
 * panel, and a second copy here is exactly how the two surfaces would come to
 * disagree about what "Asian" means -- the panel calling a style AVERAGE_STRIKE
 * while a leg spelt it AVG_STRIKE would compile on both sides and only diverge
 * on the wire.
 *
 * A TYPE-ONLY import, which `isolatedModules` erases entirely at emit, so it
 * cannot form the runtime cycle a value import would: `useTreePricerStore`
 * imports THIS module for spot, strike and rate.
 */
import type { AsianStyle } from './useTreePricerStore';

export interface Leg {
  id: string;
  instrument_type: string;
  action: string;
  option_type: string;
  strike_price: number;
  premium: number;
  quantity: number;
  expiration_days?: number;
  implied_volatility?: number;
  /**
   * Averaging style. Absent means NOT_ASIAN -- the same meaning the proto's
   * zero value carries, so a leg built before this field existed (the strategy
   * templates in `StrategySelector` still build legs without it) keeps its
   * current wire meaning and its current answers.
   */
  asian_type?: AsianStyle;
}

/** A point on the at-expiry payoff curve, exactly as the engine returns it. */
export interface CurvePoint {
  price: number;
  pnl: number;
}

/**
 * Where the risk-free rate came from. The distinction is the whole point:
 * 'measured' is an observation with a date and a source behind it, 'user' is a
 * number somebody typed. Collapsing the two is what let a hardcoded 0.05 sit
 * behind expected value, probability of profit and the distribution curve
 * looking exactly like a quoted figure (spec §3.4).
 */
export type RateSource = 'pending' | 'measured' | 'user' | 'unavailable';

export interface RateMeta {
  tenor: string;      // "3M"
  asOfDate: string;   // "2026-07-29" — the observation date, not when we asked
  source: string;     // "us_treasury_par_yield"
  published: number;  // as published (bond-equivalent), decimal
  fetchedAt: string;  // RFC3339
}

/**
 * One leg's contribution to position risk. Maps 1:1 onto `LegRisk`.
 *
 * These sum to the aggregate Greeks exactly — the engine accumulates both in
 * one pass from the same numbers, and the smoke gate asserts the reconciliation.
 * Worth stating because the breakdown is only useful if it is the same
 * computation, not a second opinion.
 */
export interface LegRisk {
  legIndex: number;
  delta: number;
  gamma: number;
  theta: number;
  vega: number;
  /** Model price now, per share. Zero for a linear leg, which has none. */
  modelPrice: number;
  /** Model value against what the leg cost, scaled to the position. */
  openPnl: number;
}

/** One cell of the price x date grid. Maps 1:1 onto `MatrixCell`. */
export interface MatrixCell {
  price: number;
  daysToExpiration: number;
  /** ISO-8601 YYYY-MM-DD. */
  date: string;
  pnl: number;
  returnOnRiskPercent: number;
}

/**
 * The engine's answer.
 *
 * Every field here maps 1:1 onto a field of `StrategyResponse`. The previous
 * shape carried a `probability_density: 0.5` and a `days_to_expiration: 30`
 * that no RPC ever produced — they were invented during mapping, which is
 * precisely the failure mode spec §3.4 exists to prevent.
 */
export interface CalculationResult {
  /** At-expiry P&L across the price axis. `StrategyResponse.pnl_matrix`. */
  expiryCurve: CurvePoint[];
  /**
   * The price x date grid. `StrategyResponse.matrix`.
   *
   * The engine has always computed this — every leg re-priced at every future
   * date — and the UI rendered only the one-dimensional curve above, so the
   * work was thrown away on arrival. It is the only view in which a calendar
   * or diagonal spread is legible at all, because those positions are defined
   * by how their value moves BETWEEN today and the near expiry.
   */
  matrix: MatrixCell[];
  /** Per-leg risk, indexed by position in `legs`. */
  legRisk: LegRisk[];
  max_profit: number;
  max_loss: number;
  break_even: number;
  expected_value: number;
  /** Probability of profit — sensen integrates a lognormal over profitable regions. */
  pop: number;
  risk_reward_ratio: number;
  aggregate_greeks: {
    delta: number;
    gamma: number;
    theta: number;
    vega: number;
    rho: number;
  };
  risk: {
    var95: number;
    var99: number;
    cvar95: number;
    cvar99: number;
  };
  /**
   * The inputs this result was computed from, echoed back so downstream panels
   * (the probability distribution in particular) model the terminal price
   * using the same numbers the engine used, rather than a second guess.
   */
  inputs: {
    spot: number;
    impliedVolatility: number;
    /** Latest leg expiry: the horizon the position runs to. */
    days: number;
    /**
     * The date the payoff curve — and every figure derived from it, including
     * PoP — is evaluated at, in days from now. This is the EARLIEST leg expiry,
     * so it equals `days` for a single-expiry position and is shorter for a
     * calendar or diagonal. Anything modelling the terminal price distribution
     * must use this, not `days`, or it shades a different distribution from the
     * one the engine's probabilities came from.
     */
    curveDays: number;
    /** The rate the engine priced with. Continuous when measured, as-typed when stated. */
    riskFreeRate: number;
    /**
     * Continuous dividend yield the engine priced with. Always an assumption —
     * no provider wired into this engine publishes a forward-looking yield — so
     * it is presented as stated, never as measured.
     */
    dividendYield: number;
    rateSource: RateSource;
    rateMeta: RateMeta | null;
  };
}

/* ---------------------------------------------------------------------------
   Option chain.

   The chain lives in the store rather than inside the chain component, because
   two other surfaces need it: the strategy templates price their legs from it,
   and the probability curve depends on the IV those legs carry. Keeping it
   local to the table forced the templates to invent premiums.
   ------------------------------------------------------------------------- */

export interface ChainQuote {
  bid: number;
  ask: number;
  delta: number;
  iv: number;
  volume: number;
  openInterest: number;
}

export interface ChainStrike {
  strike: number;
  isAtm: boolean;
  call: ChainQuote;
  put: ChainQuote;
}

export interface ChainExpiration {
  date: string;
  dte: number;
  label: string;
}

/**
 * One contract on the futures forward curve. Maps 1:1 onto `FuturesContract`.
 *
 * DERIVED, not quoted. Forward price is cost-of-carry from a measured spot and
 * the measured Treasury rate; `state` carries MODELLED for exactly that reason.
 * bid, ask, volume and open interest are order-book facts no formula produces
 * and arrive as zero — rendered as an em dash, never as a number.
 */
export interface FuturesContract {
  code: string;
  deliveryMonth: string;
  daysToExpiry: number;
  futuresPrice: number;
  basis: number;
  annualizedYield: number;
  state: string;
}

export type ChainStatus = 'idle' | 'loading' | 'ready' | 'error';

/**
 * The order ticket — a leg being composed, before it joins the position.
 *
 * Previously a chain click appended a leg immediately, with strike, premium,
 * quantity and IV fixed at whatever the row happened to hold. That made the
 * 116-row ladder the only way to express an option and left three of those
 * four values uneditable. The ticket makes the option a form: the chain fills
 * it in, and every field stays editable before it is committed.
 */
export interface TicketDraft {
  action: 'BUY' | 'SELL';
  optionType: 'CALL' | 'PUT';
  /** Expiration date, ISO-8601. Empty until a chain is loaded. */
  expiration: string;
  strike: number | null;
  /** Per-share price. Null means "not quoted" — never silently zero. */
  premium: number | null;
  quantity: number;
  impliedVolatility: number | null;
  /**
   * Averaging style for the leg being composed. Defaults to 'NOT_ASIAN' --
   * a vanilla option is what a trader who never touches the control meant,
   * and it is also the proto's zero value, so the default costs nothing on
   * the wire.
   */
  asianType: AsianStyle;
}

interface CalculatorState {
  symbol: string;
  assetClass: 'EQUITY' | 'FUTURES' | 'CRYPTO';
  legs: Leg[];
  spotPrice: number;
  riskFreeRate: number | null;
  rateSource: RateSource;
  /** Continuous dividend yield, decimal. Zero means no dividend is modelled. */
  dividendYield: number;
  rateMeta: RateMeta | null;
  result: CalculationResult | null;
  isLoading: boolean;
  error: string | null;

  /**
   * A PRECONDITION the user has not met yet -- no strike picked, no expiry on
   * the ticket -- as opposed to something that went wrong.
   *
   * Separate from `error` because the panel renders them differently and must:
   * `error` is "Unavailable" in loss red, which tells a trader the calculator
   * is broken. "You have not chosen a strike yet" is not a failure, it is the
   * next thing to do, and saying it in red is the same defect this project
   * already fixed once when PERMISSION_DENIED rendered under "Unavailable"
   * instead of the upgrade prompt.
   */
  notReady: string | null;

  /**
   * The engine's own words when it refused this position for ENTITLEMENT
   * reasons, rather than for anything wrong with the inputs.
   *
   * Separate from `error` because the two want opposite treatments: an error
   * is something the user should fix, and this is something they can buy. It
   * holds the server's message verbatim rather than a client-side rewording,
   * for the same reason `StrategySelector` marks multi-leg strategies without
   * disabling them -- the refusal is the engine's to make and to explain, and
   * a paraphrase here would drift from what the gate actually did.
   *
   * Written ONLY by `calculateStrategy`, which clears it on entry before any
   * of its early returns, so a denial cannot outlive the position that
   * provoked it.
   */
  gateDenied: string | null;

  /**
   * The engine's explanation for a position it declines to model, as opposed
   * to one it failed to price. Set only from gRPC FAILED_PRECONDITION.
   *
   * Separate from `error` because the two mean opposite things to a trader:
   * `error` says the app is broken and retrying might work, this says the
   * position is fine and the model does not cover it. Rendering the second as
   * the first is how a documented limitation reads as an outage.
   */
  modelLimit: string | null;

  chainStrikes: ChainStrike[];
  futuresCurve: FuturesContract[];
  chainExpirations: ChainExpiration[];
  selectedExpiration: string;
  chainStatus: ChainStatus;
  chainError: string | null;
  chainFetchedAt: string | null;

  ticket: TicketDraft;

  setSymbol: (symbol: string, spotPrice?: number, assetClass?: 'EQUITY' | 'FUTURES' | 'CRYPTO') => void;
  addLeg: (leg: Omit<Leg, 'id'>) => void;
  removeLeg: (id: string) => void;
  clearLegs: () => void;
  updateLeg: (id: string, updates: Partial<Leg>) => void;
  setSpotPrice: (price: number) => void;
  setRiskFreeRate: (rate: number) => void;
  setDividendYield: (q: number) => void;
  loadRiskFreeRate: () => Promise<void>;
  loadChain: (expiration?: string) => void;
  setSelectedExpiration: (date: string) => void;
  setTicket: (patch: Partial<TicketDraft>) => void;
  commitTicket: () => void;

  saveStrategy: (name: string, symbol: string) => Promise<void>;
  loadStrategies: () => Promise<void>;
  calculateStrategy: () => Promise<void>;
}

const supabase = createClient();

/**
 * Symbol -> asset class classification.
 *
 * This is reference data about *what an instrument is*, not a market
 * observation, so hardcoding it is legitimate. The previous build also carried
 * a hardcoded price for each symbol (SPY at 580.0 against a real 738.93); those
 * have been removed. Prices come from the backend quote service only — see
 * spec §3.4, real data only.
 */
const FUTURES_SYMBOLS = ['ES', 'NQ', 'RTY', 'YM', 'CL', 'NG', 'GC', 'SI', 'ZB', 'ZN'];
const CRYPTO_SYMBOLS = ['BTC', 'ETH', 'SOL'];

function classify(symbol: string): 'EQUITY' | 'FUTURES' | 'CRYPTO' {
  if (FUTURES_SYMBOLS.includes(symbol)) return 'FUTURES';
  if (CRYPTO_SYMBOLS.includes(symbol)) return 'CRYPTO';
  return 'EQUITY';
}

/**
 * Position-level implied volatility.
 *
 * Taken as the quantity-weighted mean of the per-leg IVs that came off the
 * option chain. Returns null when no leg carries a chain IV — the caller must
 * then refuse to calculate rather than substitute a plausible 0.20, which is
 * what the previous build did on every single request.
 */
function positionIv(legs: Leg[]): number | null {
  const priced = legs.filter((l) => (l.implied_volatility ?? 0) > 0);
  if (priced.length === 0) return null;
  const totalQty = priced.reduce((s, l) => s + Math.abs(l.quantity), 0);
  if (totalQty === 0) return null;
  return (
    priced.reduce((s, l) => s + (l.implied_volatility as number) * Math.abs(l.quantity), 0) /
    totalQty
  );
}

/** Longest-dated leg — the horizon the whole structure is measured to. */
function horizonDays(legs: Leg[]): number {
  return legs.reduce((m, l) => Math.max(m, l.expiration_days ?? 0), 0);
}

/**
 * The in-flight rate request, shared by every caller.
 *
 * The rate is one global datum, so concurrent callers must not each open their
 * own RPC — the header mounting and a widget calculating at the same time is
 * the ordinary case, not an edge one. Holding the promise at module scope means
 * the second caller awaits the first request instead of racing it. Cleared on
 * settle so a later retry after a failure is still possible.
 */
let rateRequest: Promise<void> | null = null;

/**
 * gRPC `PERMISSION_DENIED`.
 *
 * Spelled out rather than imported from grpc-web's `StatusCode`: that enum is
 * a runtime value, and importing it here would pull the whole module into a
 * store that otherwise only needs the generated client. The number is fixed by
 * the gRPC specification, not by any library version.
 */
const RPC_PERMISSION_DENIED = 7;

// FAILED_PRECONDITION. The engine says the position is real and well formed
// but outside what this model can describe -- today that means an Asian leg,
// whose payoff is a function of the average price rather than the price at
// expiry. It is NOT a failure and must not render in loss-red under
// "Unavailable": nothing is broken and retrying will not help. Kept separate
// from `error` for the same reason `gateDenied` is, and matched on the CODE
// because the sentence is copy and copy gets reworded.
const RPC_FAILED_PRECONDITION = 9;

export const useCalculatorStore = create<CalculatorState>((set, get) => ({
  symbol: 'SPY',
  assetClass: 'EQUITY',
  legs: [],
  // Unknown until the quote service answers. Never seeded with a stand-in price.
  spotPrice: 0,
  // Unknown until the rate service answers, exactly like spotPrice above. The
  // previous 0.05 was invisible, unsourced, and still shaped every probability
  // the UI displayed.
  riskFreeRate: null,
  rateSource: 'pending',
  dividendYield: 0,
  rateMeta: null,
  result: null,
  isLoading: false,
  error: null,
  notReady: null,
  gateDenied: null,
  modelLimit: null,

  chainStrikes: [],
  chainExpirations: [],
  futuresCurve: [],
  selectedExpiration: '',
  chainStatus: 'idle',
  chainError: null,
  chainFetchedAt: null,

  // Zero is not a price. The ticket starts empty and the chain fills it in;
  // committing with a null premium is refused rather than defaulted, because a
  // free option is the one thing the payoff maths cannot recover from.
  ticket: {
    action: 'BUY',
    optionType: 'CALL',
    expiration: '',
    strike: null,
    premium: null,
    quantity: 1,
    impliedVolatility: null,
    asianType: 'NOT_ASIAN',
  },

  setSymbol: (symbolInput, customPrice, customAssetClass) => {
    const sym = symbolInput.trim().toUpperCase();

    // A user-supplied simulation price is an explicit override and is honoured.
    // Otherwise the price is unknown until the quote service answers — it is
    // NOT defaulted to a plausible-looking number.
    set({
      symbol: sym,
      spotPrice: customPrice !== undefined ? customPrice : 0,
      assetClass: customAssetClass || classify(sym),
      error: null,
      notReady: null,
      // The old chain belongs to the old symbol. Showing it against the new one
      // would be the most convincing kind of wrong data.
      chainStrikes: [],
      chainExpirations: [],
      // The forward curve is part of that chain and was missed here. Switching
      // from ES to a root with no futures source left the E-mini curve on
      // screen under the new symbol's header — a real curve for an instrument
      // the user was no longer looking at.
      futuresCurve: [],
      selectedExpiration: '',
      chainStatus: 'idle',
      chainError: null,
      chainFetchedAt: null,
      result: null,
    });

    get().loadChain();

    if (customPrice !== undefined) return;

    const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);
    const req = new QuoteRequest();
    req.setSymbol(sym);
    // The ticker alone is ambiguous and the backend cannot guess: "ES" is both
    // the E-mini S&P root and Eversource Energy's NYSE symbol. Sending the class
    // the user is actually working in is what stops a futures screen from being
    // priced off a utility company that happens to share the ticker.
    req.setAssetClass(customAssetClass || classify(sym));

    client.getMarketQuote(req, authMetadata(), (err, res) => {
      // Same staleness guard loadChain applies below. Without it a response for
      // a symbol the user has already navigated away from still writes its
      // price, so the store shows the new symbol carrying the old symbol's
      // spot — a real number attributed to the wrong instrument, which then
      // prices every leg.
      if (get().symbol !== sym) return;

      if (err || !res || res.getPrice() <= 0) {
        // Surface the failure. Substituting a fallback price here is what
        // previously let fabricated data reach the screen unnoticed.
        set({
          spotPrice: 0,
          error: `No quote available for ${sym}${err?.message ? `: ${err.message}` : ''}`,
          // Cleared with it. The invariant is that `error` and `notReady`
          // are never both truthy -- scoped to that PAIR, not to refusals in
          // general: `gateDenied` and `modelLimit` can legitimately stand
          // beside a notReady, and all five panels name the gate first when
          // they do. It has to hold in BOTH directions: every notReady write
          // clears `error`, so every `error` write must clear `notReady`.
          // Without this the two render side by side -- switch symbol, press
          // Add before the quote lands (strike and premium survive the switch,
          // so the expiry guard fires), then let the quote fail. With no legs
          // the Recalculate button is disabled and StrategyWorkspace only
          // recalculates above zero legs, so the stale prompt has no way out.
          notReady: null,
        });
        return;
      }
      set({ spotPrice: res.getPrice(), error: null });
    });
  },

  addLeg: (leg) => set((state) => ({
    legs: [...state.legs, { ...leg, id: Math.random().toString(36).substring(7) }]
  })),

  // Emptying the leg set clears the denial and the model limit with it.
  //
  // `calculateStrategy` clears both at its top, which covers every path that
  // recalculates — and `StrategyWorkspace` recalculates on `legs.length > 0`,
  // so the ONE case it does not cover is the leg set becoming empty. That is
  // the case where a stale denial is most visible: the user reads "Needs Pro"
  // with its checkout buttons over a position they have just deleted, and
  // nothing they can do to an empty ticket will clear it.
  //
  // Removing a leg down to zero is the same event as pressing Clear, so it is
  // handled the same way. Removing down to one or more is not, because the
  // recalculation that follows does the clearing itself.
  removeLeg: (id) => set((state) => {
    const legs = state.legs.filter(l => l.id !== id);
    return legs.length === 0
      ? { legs, result: null, gateDenied: null, modelLimit: null, notReady: null }
      : { legs };
  }),

  clearLegs: () => set({ legs: [], result: null, gateDenied: null, modelLimit: null, notReady: null }),

  updateLeg: (id, updates) => set((state) => ({
    legs: state.legs.map(l => l.id === id ? { ...l, ...updates } : l)
  })),

  setSpotPrice: (price) => set({ spotPrice: price }),

  // A manual edit is a stated assumption, never a measurement. The typed value
  // is used exactly as given: we do not convert it between compounding
  // conventions, because we cannot know which one the user meant and picking
  // one would silently restate their input.
  setRiskFreeRate: (rate) => set({ riskFreeRate: rate, rateSource: 'user' }),
  setDividendYield: (q) => set({ dividendYield: q >= 0 ? q : 0 }),

  /**
   * Fetch the measured risk-free rate.
   *
   * Lives in the store rather than in a component effect because it is a
   * precondition of calculating, not a decoration of one particular header.
   * When the header owned it, every route that computes without rendering the
   * header — `/widget` does exactly that — left the rate permanently `null` and
   * so could never produce a result at all. `calculateStrategy` awaits this
   * itself, which makes any future route correct by construction instead of
   * obliging it to remember a setup call.
   */
  loadRiskFreeRate: () => {
    if (rateRequest) return rateRequest;

    const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);

    rateRequest = new Promise<void>((resolve) => {
      client.getRiskFreeRate(new RiskFreeRateRequest(), authMetadata(), (err, res) => {
        // A rate the user has already typed outranks a late arrival: it was an
        // explicit decision, and overwriting it would move a number out from
        // under the cursor.
        if (get().rateSource === 'user') {
          resolve();
          return;
        }

        // Gate on the observation date, not on the value being positive. A zero
        // or negative short rate is a real observation — US 3-month bills have
        // printed at 0.00 — so rejecting one would be a policy judgement dressed
        // up as a data check. A response with no as_of_date is the unusable case,
        // and the backend already refuses to send one.
        if (err || !res || !res.getAsOfDate()) {
          set({ riskFreeRate: null, rateSource: 'unavailable', rateMeta: null });
          resolve();
          return;
        }

        set({
          // The continuous figure is what Black-Scholes wants, and it is also
          // what is displayed: the published par yield is a different number
          // (bond-equivalent, semiannual) and showing that in an editable field
          // while pricing with this one meant a single keystroke silently
          // re-entered the published figure as if it were continuous. Both are
          // stated in the chip tooltip; only the one in use is in the slot.
          riskFreeRate: res.getRate(),
          rateSource: 'measured',
          rateMeta: {
            tenor: res.getTenor(),
            asOfDate: res.getAsOfDate(),
            source: res.getSource(),
            published: res.getRatePublished(),
            fetchedAt: res.getFetchedAt(),
          },
        });
        resolve();
      });
    }).finally(() => {
      // Allow a later retry: a failed fetch must not latch 'unavailable' for
      // the lifetime of the tab.
      rateRequest = null;
    });

    return rateRequest;
  },

  setTicket: (patch) => set((st) => ({ ticket: { ...st.ticket, ...patch } })),

  /**
   * Move the ticket into the position.
   *
   * Refuses rather than substituting: an option with no strike cannot be
   * priced, and one with no premium is implicitly free, which silently makes
   * every payoff figure wrong. Both were possible before, because a chain row
   * with no quote appended a leg carrying zeros.
   */
  commitTicket: () => {
    const t = get().ticket;
    if (t.strike === null || t.strike <= 0) {
      set({ notReady: 'Pick a strike before adding the leg.', error: null });
      return;
    }
    if (t.premium === null || t.premium <= 0) {
      set({ notReady: 'This contract has no quoted price. Enter the price you would pay or receive.', error: null });
      return;
    }
    // Resolve the expiration exactly as OptionTicket displays it. Reading only
    // `t.expiration` here is what produced the defect this guard now closes:
    // the lookup missed, `?? 0` turned the miss into `expiration_days: 0`, and
    // the leg went into the position looking complete -- strike, premium and
    // quantity all present. Every downstream panel (payoff, P&L matrix, P&L
    // surface, probability distribution, outcome) then refused with "No
    // expiration on any leg -- pick an expiry from the chain" while the Expiry
    // dropdown sat there showing the date the user had supposedly not picked.
    const expiration = t.expiration || get().selectedExpiration;
    const match = get().chainExpirations.find((e) => e.date === expiration);
    if (!match) {
      // Refuse rather than substitute, the same rule the strike and premium
      // guards above follow. A fabricated 0 is not a safer default than an
      // error: it is an error that has been made to look like an answer.
      set({ notReady: 'Pick an expiry before adding the leg.', error: null });
      return;
    }
    const dte = match.dte;
    get().addLeg({
      instrument_type: 'INSTRUMENT_EQUITY_OPTION',
      action: t.action,
      option_type: t.optionType,
      strike_price: t.strike,
      premium: t.premium,
      quantity: t.quantity > 0 ? t.quantity : 1,
      expiration_days: dte,
      implied_volatility: t.impliedVolatility ?? undefined,
      // Carried, not defaulted. The engine refuses an Asian leg today, and
      // that refusal is the honest answer -- dropping the style here instead
      // would put a vanilla leg into the position under a ticket that said
      // Asian, which is the same class of silent substitution as the
      // fabricated `expiration_days: 0` this guard block exists to prevent.
      asian_type: t.asianType,
    });
    set({ error: null, notReady: null });
  },

  setSelectedExpiration: (date) => {
    set({ selectedExpiration: date });
    get().loadChain(date);
  },

  loadChain: (expiration) => {
    const { symbol, assetClass, selectedExpiration } = get();
    const wanted = expiration ?? selectedExpiration;

    set({ chainStatus: 'loading', chainError: null });

    const backendUrl =
      process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);
    const req = new ChainRequest();
    req.setSymbol(symbol);
    req.setAssetClass(assetClass);
    if (wanted) req.setExpirationDate(wanted);

    client.getMarketChain(req, authMetadata(), (err, res) => {
      // A response for a symbol the user has since navigated away from must be
      // discarded, not rendered.
      if (get().symbol !== symbol) return;

      if (err || !res) {
        set({
          chainStrikes: [],
          chainExpirations: [],
          // A failed load must leave nothing behind either. Keeping the last
          // successful curve here would show a stale one beside an error.
          futuresCurve: [],
          chainStatus: 'error',
          chainError: err?.message || 'Chain service unreachable',
          chainFetchedAt: null,
        });
        return;
      }

      const resolvedExpiration = wanted || res.getSelectedExpirationDate() || '';

      const expirations: ChainExpiration[] = res.getAvailableExpirationsList().map((e) => ({
        date: e.getDateStr(),
        dte: e.getDaysToExpiry(),
        label: e.getLabel(),
      }));

      const strikes: ChainStrike[] = res.getOptionStrikesList().map((s) => ({
        strike: s.getStrike(),
        isAtm: s.getIsAtm(),
        call: {
          bid: s.getCallBid(), ask: s.getCallAsk(), delta: s.getCallDelta(),
          iv: s.getCallIv(), volume: s.getCallVolume(), openInterest: s.getCallOpenInterest(),
        },
        put: {
          bid: s.getPutBid(), ask: s.getPutAsk(), delta: s.getPutDelta(),
          iv: s.getPutIv(), volume: s.getPutVolume(), openInterest: s.getPutOpenInterest(),
        },
      }));

      set({
        chainExpirations: expirations,
        chainStrikes: strikes,
        futuresCurve: res.getFuturesContractsList().map((c) => ({
          code: c.getCode(),
          deliveryMonth: c.getDeliveryMonth(),
          daysToExpiry: c.getDaysToExpiry(),
          futuresPrice: c.getFuturesPrice(),
          basis: c.getBasis(),
          annualizedYield: c.getAnnualizedYield(),
          state: c.getState(),
        })),
        selectedExpiration: resolvedExpiration,
        chainFetchedAt: typeof res.getFetchedAt === 'function' ? res.getFetchedAt() || null : null,
        // Seed the ticket's own expiration from the same value in the same
        // breath. OptionTicket DISPLAYS `ticket.expiration || selectedExpiration`
        // but commitTicket read only `ticket.expiration`, so a user who never
        // touched the Expiry dropdown -- the default path -- committed a leg
        // whose expiration was '' while the dropdown visibly read a real date.
        // Keeping the two in lockstep here means the displayed value and the
        // stored value cannot disagree in the first place.
        ticket: { ...get().ticket, expiration: resolvedExpiration },
        // A futures symbol returns a forward curve and no option strikes. The
        // strikes-only test called that an error and blanked the panel.
        chainStatus:
          strikes.length > 0 || res.getFuturesContractsList().length > 0 ? 'ready' : 'error',
        chainError:
          strikes.length > 0 || res.getFuturesContractsList().length > 0
            ? null
            : `No listed contracts returned for ${symbol}`,
      });
    });
  },

  calculateStrategy: async () => {
    // Cleared HERE, above every early return below, rather than alongside the
    // `error: null` in each of them. A denial belongs to one position; leaving
    // it set while the user empties their legs would show an upgrade prompt
    // for a position that no longer exists, and the early returns are exactly
    // the paths that would have skipped a clear placed further down.
    // modelLimit clears with it, and for the same reason: it describes the
    // position that provoked it, so leaving it set would explain a limitation
    // of a position the user has since changed.
    set({ gateDenied: null, modelLimit: null, notReady: null });

    // The rate is deliberately not destructured here: it is read after the
    // fetch below, so a snapshot taken now would be the pre-fetch null.
    const { legs, spotPrice, symbol } = get();

    if (legs.length === 0) {
      set({ result: null, error: null, notReady: null });
      return;
    }

    // Refuse to compute on inputs we do not actually have. Each of these was
    // previously hardcoded into the request ('SPY', 0.20, 30 days), so the
    // engine returned a real answer to a fabricated question.
    if (spotPrice <= 0) {
      set({ result: null, notReady: `No spot price for ${symbol} — cannot price the position.`, error: null });
      return;
    }
    const iv = positionIv(legs);
    if (iv === null) {
      // Same distinction the expiry guard below draws. Telling someone to "add
      // legs from the option chain" is only useful advice when they have not;
      // a position that already holds legs got here because the CONTRACTS
      // carry no published IV, which is the ordinary case for a same-day
      // expiry -- implied volatility is undefined as time to expiry vanishes,
      // so the feed omits it rather than inventing one. The remedy there is
      // the ticket's own IV field, not the chain.
      set({
        result: null,
        notReady:
          legs.length > 0
            ? 'These contracts publish no implied volatility — common at a same-day expiry. Enter an IV in the ticket, or choose a later expiration.'
            : 'No implied volatility on any leg. Add legs from the option chain so IV and premium come from live quotes.',
        error: null,
      });
      return;
    }
    const days = horizonDays(legs);
    if (days <= 0) {
      // Two different causes reach this branch and they need different words.
      // A leg with no `expiration_days` at all genuinely has no expiry. A leg
      // carrying 0 was given one -- the chain lists a same-day expiration, and
      // picking it is a legitimate choice, not an omission. Telling that user
      // to "pick an expiry from the chain" denies what they just did.
      const anyExpiry = legs.some((l) => l.expiration_days !== undefined);
      set({
        result: null,
        notReady: anyExpiry
          ? 'Every leg expires today. The payoff model prices remaining time value, so it needs at least one day to expiry — pick a later expiration.'
          : 'No expiration on any leg — pick an expiry from the chain.',
        error: null,
      });
      return;
    }
    // Fetch the rate on demand if nothing has yet. This is what makes the rate
    // a precondition of the calculation rather than a side effect of rendering
    // one particular component, so a route that computes without the header
    // still gets a measured rate.
    if (get().rateSource === 'pending') {
      await get().loadRiskFreeRate();
    }

    // Same rule as spot and IV: refuse rather than answer a question we were
    // not asked. The rate is no longer a constant we can fall back on, and
    // inventing one would put a fabricated number behind PoP, expected value
    // and the whole distribution. Re-read after the await — the fetch above
    // resolves into the store, not into the destructured snapshot.
    const rate = get().riskFreeRate;
    if (rate === null) {
      // Distinguish the two cases: saying the feed is unavailable while the
      // request is still in flight asserts a failure that has not happened.
      set({
        result: null,
        error:
          get().rateSource === 'pending'
            ? 'Still fetching the Treasury rate — retry in a moment.'
            : 'No risk-free rate — the Treasury feed is unavailable. Enter a rate to proceed; it will be labelled an assumption.',
        // This guard sits AFTER an await, so the clear at the top of
        // calculateStrategy is not enough: a notReady set between the two
        // would survive into this failure.
        notReady: null,
      });
      return;
    }

    set({ isLoading: true, error: null });
    try {
      const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
      const client = new OptionsCalculatorClient(backendUrl);
      const req = new StrategyRequest();
      req.setUnderlyingSymbol(symbol);
      req.setCurrentPrice(spotPrice);
      req.setImpliedVolatility(iv);
      req.setRiskFreeRate(rate);
      // Always sent, including zero. Zero is meaningful — it says 'no dividend
      // modelled' — and the engine's q == 0 path is bit-for-bit plain
      // Black-Scholes, so this cannot perturb a non-dividend position.
      req.setDividendYield(get().dividendYield);

      // Averaging style onto the wire enum. Written as an exhaustive record
      // rather than a chain of ternaries so that adding a style to
      // `AsianStyle` fails to compile here instead of silently falling
      // through to NOT_ASIAN -- the one value that means "there is nothing
      // special about this leg".
      const WIRE_ASIAN_TYPE: Record<AsianStyle, ProtoLeg.AsianType> = {
        NOT_ASIAN: ProtoLeg.AsianType.NOT_ASIAN,
        AVERAGE_PRICE: ProtoLeg.AsianType.AVERAGE_PRICE,
        AVERAGE_STRIKE: ProtoLeg.AsianType.AVERAGE_STRIKE,
      };

      const legType = (l: Leg) => {
        switch (l.option_type) {
          case 'CALL': return ProtoLeg.Type.CALL;
          case 'PUT': return ProtoLeg.Type.PUT;
          case 'FUTURE': return ProtoLeg.Type.FUTURE;
          default: return ProtoLeg.Type.STOCK;
        }
      };

      req.setLegsList(
        legs.map((l) => {
          const isOption = l.option_type === 'CALL' || l.option_type === 'PUT';
          const pLeg = new ProtoLeg();
          pLeg.setAction(l.action === 'BUY' ? ProtoLeg.Action.BUY : ProtoLeg.Action.SELL);
          pLeg.setType(legType(l));
          pLeg.setStrike(l.strike_price);
          pLeg.setExpirationDays(l.expiration_days ?? days);
          pLeg.setQuantity(l.quantity);
          // The entry price and per-leg IV had no field on the old proto, so
          // every leg reached the engine priced at zero with no volatility.
          pLeg.setPremium(l.premium);
          pLeg.setImpliedVolatility(l.implied_volatility ?? 0);
          pLeg.setContractMultiplier(isOption ? 100 : 1);
          // Sent for EVERY leg, not only option legs. A futures or stock leg
          // has no averaging window, so nothing in this client ever marks one
          // Asian -- but suppressing the field here on a leg that did carry a
          // style would drop it silently, and the engine is the side that gets
          // to say a leg is malformed. `averaging_states` is deliberately not
          // sent: zero means "engine default", and this UI has no control for
          // it, so sending a client-chosen grid size would be an assumption
          // the trader never made.
          pLeg.setAsianType(WIRE_ASIAN_TYPE[l.asian_type ?? 'NOT_ASIAN']);
          return pLeg;
        }),
      );

      const res = await client.calculateStrategy(req, authMetadata());

      const expiryCurve: CurvePoint[] = res.getPnlMatrixList().map((p) => ({
        price: p.getUnderlyingPrice(),
        pnl: p.getPnl(),
      }));

      const matrix: MatrixCell[] = res.getMatrixList().map((c) => ({
        price: c.getPrice(),
        daysToExpiration: c.getDaysToExpiration(),
        date: c.getDateStr(),
        pnl: c.getPnlDollars(),
        returnOnRiskPercent: c.getReturnOnRiskPercent(),
      }));

      const legRisk: LegRisk[] = res.getLegRiskList().map((lr) => {
        const lg = lr.getGreeks();
        return {
          legIndex: lr.getLegIndex(),
          delta: lg?.getDelta() ?? 0,
          gamma: lg?.getGamma() ?? 0,
          theta: lg?.getTheta() ?? 0,
          vega: lg?.getVega() ?? 0,
          modelPrice: lr.getModelPrice(),
          openPnl: lr.getOpenPnl(),
        };
      });

      const g = res.getNetGreeks();
      const rm = res.getRiskMetrics();
      const maxLoss = res.getMaxLoss();

      set({
        isLoading: false,
        error: null,
        // Cleared on SUCCESS too. The entry clear at the top of this action
        // happens before two awaits, so a prompt raised in either window would
        // otherwise survive onto a live result -- the two exits from that same
        // window were treated differently, failure clearing and success not.
        notReady: null,
        result: {
          expiryCurve,
          matrix,
          legRisk,
          max_profit: res.getMaxProfit(),
          max_loss: maxLoss,
          break_even: res.getBreakEven(),
          expected_value: res.getExpectedValue(),
          pop: res.getPop(),
          risk_reward_ratio: maxLoss !== 0 ? Math.abs(res.getMaxProfit() / maxLoss) : 0,
          aggregate_greeks: {
            delta: g?.getDelta() ?? 0,
            gamma: g?.getGamma() ?? 0,
            theta: g?.getTheta() ?? 0,
            vega: g?.getVega() ?? 0,
            rho: g?.getRho() ?? 0,
          },
          risk: {
            var95: rm?.getVarParametric95() ?? 0,
            var99: rm?.getVarParametric99() ?? 0,
            cvar95: rm?.getCvarParametric95() ?? 0,
            cvar99: rm?.getCvarParametric99() ?? 0,
          },
          // What the engine was actually given, so the panel can state the
          // inputs rather than a snapshot taken before the rate resolved.
          inputs: {
            spot: spotPrice,
            impliedVolatility: iv,
            days,
            // Falls back to the horizon when the field is absent: proto3 gives
            // an unset double as 0, which is indistinguishable from a backend
            // that predates the field, and 0 would collapse the distribution.
            curveDays: res.getCurveDaysToExpiration() || days,
            riskFreeRate: rate,
            dividendYield: get().dividendYield,
            rateSource: get().rateSource,
            rateMeta: get().rateMeta,
          },
        },
      });
    } catch (err: unknown) {
      const message = (err as Error).message || 'Calculation failed';

      // PERMISSION_DENIED (7) is the entitlement refusal, and on THIS client it
      // can only be one. The engine returns code 7 from exactly three places
      // (backend/src/modules/api_key.cpp): the multi-leg gate, the assistant
      // gate -- which this store never calls -- and a licence that failed to
      // verify. Every one of those means "you do not have Pro, or your Pro has
      // lapsed", so every one of them wants the same response: the engine's
      // sentence, and a way to fix it. Quota refusals are RESOURCE_EXHAUSTED
      // (8) and stay ordinary errors, because paying does not fix a rate limit.
      //
      // Matched on the CODE, never on the message text. The message is copy: it
      // was reworded twice in one day when the mortgage surface got its own
      // wording, and any client keyed to a substring of it would have broken
      // silently both times while still rendering something plausible.
      const code = (err as { code?: number }).code;
      if (code === RPC_PERMISSION_DENIED) {
        set({
          isLoading: false,
          result: null,
          error: null,
          gateDenied: message,
          modelLimit: null,
          notReady: null,
        });
        return;
      }

      if (code === RPC_FAILED_PRECONDITION) {
        set({
          isLoading: false,
          result: null,
          error: null,
          gateDenied: null,
          modelLimit: message,
          notReady: null,
        });
        return;
      }

      set({
        isLoading: false,
        result: null,
        error: message,
        notReady: null,
        gateDenied: null,
        modelLimit: null,
      });
    }
  },

  saveStrategy: async (name: string, symbol: string) => {
    set({ isLoading: true, error: null });
    try {
      const { legs } = get();
      const { data: { user } } = await supabase.auth.getUser();
      if (!user) throw new Error('Must be logged in to save strategies');

      const { error } = await supabase.from('saved_strategies').insert([{
        name,
        symbol,
        legs,
        user_id: user.id
      }]);

      if (error) throw error;
      set({ isLoading: false });
    } catch (err: unknown) {
      set({ isLoading: false, error: (err as Error).message || 'Failed to save strategy', notReady: null });
    }
  },

  loadStrategies: async () => {
    set({ isLoading: true, error: null });
    try {
      const { data, error } = await supabase.from('saved_strategies').select('*');
      if (error) throw error;
      console.log('Loaded strategies:', data);
      set({ isLoading: false });
    } catch (err: unknown) {
      set({ isLoading: false, error: (err as Error).message || 'Failed to load strategies', notReady: null });
    }
  }
}));
