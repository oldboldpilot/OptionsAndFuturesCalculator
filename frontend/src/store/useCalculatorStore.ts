import { create } from 'zustand';
import { createClient } from '../lib/supabase/client';
import { OptionsCalculatorClient } from '../grpc/CalculatorServiceClientPb';
import { StrategyRequest, Leg as ProtoLeg, QuoteRequest, ChainRequest, RiskFreeRateRequest } from '../grpc/calculator_pb';

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

  chainStrikes: ChainStrike[];
  chainExpirations: ChainExpiration[];
  selectedExpiration: string;
  chainStatus: ChainStatus;
  chainError: string | null;

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

  chainStrikes: [],
  chainExpirations: [],
  selectedExpiration: '',
  chainStatus: 'idle',
  chainError: null,

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
      // The old chain belongs to the old symbol. Showing it against the new one
      // would be the most convincing kind of wrong data.
      chainStrikes: [],
      chainExpirations: [],
      selectedExpiration: '',
      chainStatus: 'idle',
      chainError: null,
      result: null,
    });

    get().loadChain();

    if (customPrice !== undefined) return;

    const backendUrl = process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new OptionsCalculatorClient(backendUrl);
    const req = new QuoteRequest();
    req.setSymbol(sym);

    client.getMarketQuote(req, {}, (err, res) => {
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
        });
        return;
      }
      set({ spotPrice: res.getPrice(), error: null });
    });
  },

  addLeg: (leg) => set((state) => ({
    legs: [...state.legs, { ...leg, id: Math.random().toString(36).substring(7) }]
  })),

  removeLeg: (id) => set((state) => ({
    legs: state.legs.filter(l => l.id !== id)
  })),

  clearLegs: () => set({ legs: [], result: null }),

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
      client.getRiskFreeRate(new RiskFreeRateRequest(), {}, (err, res) => {
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
      set({ error: 'Pick a strike before adding the leg.' });
      return;
    }
    if (t.premium === null || t.premium <= 0) {
      set({ error: 'This contract has no quoted price. Enter the price you would pay or receive.' });
      return;
    }
    const dte = get().chainExpirations.find((e) => e.date === t.expiration)?.dte ?? 0;
    get().addLeg({
      instrument_type: 'INSTRUMENT_EQUITY_OPTION',
      action: t.action,
      option_type: t.optionType,
      strike_price: t.strike,
      premium: t.premium,
      quantity: t.quantity > 0 ? t.quantity : 1,
      expiration_days: dte,
      implied_volatility: t.impliedVolatility ?? undefined,
    });
    set({ error: null });
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

    client.getMarketChain(req, {}, (err, res) => {
      // A response for a symbol the user has since navigated away from must be
      // discarded, not rendered.
      if (get().symbol !== symbol) return;

      if (err || !res) {
        set({
          chainStrikes: [],
          chainExpirations: [],
          chainStatus: 'error',
          chainError: err?.message || 'Chain service unreachable',
        });
        return;
      }

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
        selectedExpiration: wanted || res.getSelectedExpirationDate() || '',
        chainStatus: strikes.length > 0 ? 'ready' : 'error',
        chainError:
          strikes.length > 0 ? null : `No listed contracts returned for ${symbol}`,
      });
    });
  },

  calculateStrategy: async () => {
    // The rate is deliberately not destructured here: it is read after the
    // fetch below, so a snapshot taken now would be the pre-fetch null.
    const { legs, spotPrice, symbol } = get();

    if (legs.length === 0) {
      set({ result: null, error: null });
      return;
    }

    // Refuse to compute on inputs we do not actually have. Each of these was
    // previously hardcoded into the request ('SPY', 0.20, 30 days), so the
    // engine returned a real answer to a fabricated question.
    if (spotPrice <= 0) {
      set({ result: null, error: `No spot price for ${symbol} — cannot price the position.` });
      return;
    }
    const iv = positionIv(legs);
    if (iv === null) {
      set({
        result: null,
        error:
          'No implied volatility on any leg. Add legs from the option chain so IV and premium come from live quotes.',
      });
      return;
    }
    const days = horizonDays(legs);
    if (days <= 0) {
      set({ result: null, error: 'No expiration on any leg — pick an expiry from the chain.' });
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
          return pLeg;
        }),
      );

      const res = await client.calculateStrategy(req, {});

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

      const g = res.getNetGreeks();
      const rm = res.getRiskMetrics();
      const maxLoss = res.getMaxLoss();

      set({
        isLoading: false,
        error: null,
        result: {
          expiryCurve,
          matrix,
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
      set({ isLoading: false, result: null, error: (err as Error).message || 'Calculation failed' });
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
      set({ isLoading: false, error: (err as Error).message || 'Failed to save strategy' });
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
      set({ isLoading: false, error: (err as Error).message || 'Failed to load strategies' });
    }
  }
}));
