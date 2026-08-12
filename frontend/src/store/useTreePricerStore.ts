/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Exercise-style and Asian-averaging tree pricer.
 *
 * Calls `sensen.finance.Finance/PriceOptionTree` over gRPC-Web -- a
 * DIFFERENT service from `calculator.OptionsCalculator`, which is what
 * `useCalculatorStore` drives. This store never touches the strategy
 * engine's own Black-Scholes pricing; it exists to show how the SAME
 * contract (spot, strike, IV, rate, expiry) prices under a trinomial tree
 * with a chosen exercise convention and, optionally, Asian averaging.
 *
 * REFUSAL DISCIPLINE mirrors `calculateStrategy` in useCalculatorStore.ts
 * exactly: no spot, no strike, no IV, or no risk-free rate all refuse to
 * price rather than substitute a plausible-looking default. Inventing an
 * input here would be exactly the failure spec section 3.4 exists to
 * prevent -- a real tree price computed from a fabricated volatility or
 * rate is indistinguishable on screen from one computed from a quote.
 */
import { create } from 'zustand';
import { useCalculatorStore } from './useCalculatorStore';

export type ExerciseStyle = 'EUROPEAN' | 'AMERICAN' | 'BERMUDAN';
export type AsianStyle = 'NOT_ASIAN' | 'AVERAGE_PRICE' | 'AVERAGE_STRIKE';
export type BermudanPreset = 'MONTHLY' | 'QUARTERLY' | 'SEMI_ANNUAL';

/**
 * One Bermudan exercise date. `days` is shown to the trader; `yearFraction`
 * is what actually goes on the wire (`OptionTreeRequest.bermudan_dates`).
 * Carrying both means the chip can show the unit a trader thinks in without
 * ever silently converting between the two at render time.
 */
export interface BermudanDate {
  id: string;
  days: number;
  yearFraction: number;
}

export interface TreeGreeks {
  delta: number;
  gamma: number;
  theta: number;
}

/**
 * One priced exercise style. `greeks` is null exactly when the REQUEST that
 * produced this result had `asian_type !== NOT_ASIAN` -- never inferred from
 * the response, because options.cppm's Asian branch returns a structural
 * `{value, 0, 0, 0}` that is byte-identical on the wire to a genuine
 * near-zero Greek. Keying off the request is the only way to tell them
 * apart honestly.
 */
export interface TreePriceResult {
  style: ExerciseStyle;
  value: number;
  greeks: TreeGreeks | null;
  /** value - the European value from the same batch. Null in Asian mode,
   *  where no European baseline is fetched (a second and third full tree
   *  solely to produce a premium figure is real cost against the shared
   *  anonymous quota bucket for a number this panel does not need). */
  earlyExercisePremium: number | null;
}

interface TreePricerState {
  exerciseType: ExerciseStyle;
  bermudanDates: BermudanDate[];

  asianExpanded: boolean;
  asianType: AsianStyle;
  averagingStates: number;

  steps: number;
  advancedOpen: boolean;

  loading: boolean;
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
  gateDenied: string | null;
  results: TreePriceResult[];
  /** Dropped Bermudan dates (out of range, or deduped against a neighbour
   *  closer than one tree step) from the LAST request actually sent, so the
   *  panel can tell the trader their date list did not do what they typed. */
  droppedDateCount: number;

  setExerciseType: (style: ExerciseStyle) => void;
  addBermudanPreset: (preset: BermudanPreset, yearsToExpiry: number) => void;
  addBermudanDay: (day: number, yearsToExpiry: number) => void;
  removeBermudanDate: (id: string) => void;
  clearBermudanDates: () => void;

  setAsianExpanded: (expanded: boolean) => void;
  setAsianType: (type: AsianStyle) => void;
  setAveragingStates: (n: number) => void;

  setSteps: (n: number) => void;
  setAdvancedOpen: (open: boolean) => void;

  /** Debounced entry point. Reads `useCalculatorStore` itself, so callers
   *  just need to invoke this whenever an input this panel depends on
   *  changes -- it is safe to call on every keystroke. */
  priceTree: () => void;
}

/** Vanilla-comparison default. Cheap: O(steps) work per style, ~40K nodes
 *  at 200 -- trivial against the shared anonymous quota bucket. */
const DEFAULT_STEPS = 100;
/** Asian pricing is O(steps^2 * states); this keeps a single Asian request
 *  a reasonable multiple of a vanilla one instead of a much larger one. */
const DEFAULT_ASIAN_STEPS = 60;
const ASIAN_STEPS_CAP = 120;
const DEFAULT_AVERAGING_STATES = 50;

const DEBOUNCE_MS = 300;

/**
 * Wire enum values, hardcoded rather than imported from `finance_pb` at
 * module scope. Verified against `finance_pb.d.ts`: OptionType{CALL=0,
 * PUT=1}, ExerciseType{EUROPEAN=0,AMERICAN=1,BERMUDAN=2},
 * AsianType{NOT_ASIAN=0,AVERAGE_PRICE=1,AVERAGE_STRIKE=2}. Keeping this
 * module free of a static `finance_pb`/`FinanceServiceClientPb` import is
 * what lets the pricing action's dynamic `import()` put the 606 KB parsed /
 * 31 KB gzipped generated client in its own chunk, instead of on the
 * critical path for every user who never opens this panel.
 */
const WIRE_OPTION_TYPE = { CALL: 0, PUT: 1 } as const;
const WIRE_EXERCISE_TYPE = { EUROPEAN: 0, AMERICAN: 1, BERMUDAN: 2 } as const;
const WIRE_ASIAN_TYPE = { NOT_ASIAN: 0, AVERAGE_PRICE: 1, AVERAGE_STRIKE: 2 } as const;

/** gRPC PERMISSION_DENIED / RESOURCE_EXHAUSTED. Spelled out for the same
 *  reason useCalculatorStore.ts does: these are fixed by the gRPC spec, not
 *  by any client library version, and importing grpc-web's StatusCode enum
 *  here would pull an extra module into a store that otherwise only needs
 *  the generated client. */
const RPC_PERMISSION_DENIED = 7;
const RPC_RESOURCE_EXHAUSTED = 8;

let debounceTimer: ReturnType<typeof setTimeout> | null = null;
/** Staleness token. Every call to `priceTree` that actually fires a request
 *  bumps this; a response is only committed to the store if the token it
 *  captured is still current, mirroring the `if (get().symbol !== sym)
 *  return;` guard in useCalculatorStore.ts. */
let requestSeq = 0;

/**
 * Filters a raw Bermudan date list down to what is actually safe to send:
 * every date strictly within (0, yearsToExpiry], deduped so two dates
 * closer together than one tree step (T/steps) collapse to one -- they are
 * one exercise right, not two, since `is_bermudan_exercise_time` only ever
 * matches a real backward-induction time within half a step.
 */
function buildValidatedBermudanDates(
  dates: BermudanDate[],
  yearsToExpiry: number,
  steps: number,
): number[] {
  if (yearsToExpiry <= 0 || steps <= 0) return [];
  const dt = yearsToExpiry / steps;
  const sorted = dates
    .map((d) => d.yearFraction)
    .filter((yf) => yf > 0 && yf <= yearsToExpiry)
    .sort((a, b) => a - b);
  const kept: number[] = [];
  for (const yf of sorted) {
    if (kept.length === 0 || yf - kept[kept.length - 1] >= dt) {
      kept.push(yf);
    }
  }
  return kept;
}

function clampSteps(steps: number, isAsian: boolean): number {
  const whole = Math.max(2, Math.floor(steps) || 2);
  return isAsian ? Math.min(whole, ASIAN_STEPS_CAP) : whole;
}

function clampAveragingStates(states: number): number {
  return Math.max(2, Math.floor(states) || 2);
}

export const useTreePricerStore = create<TreePricerState>((set, get) => ({
  /**
   * AMERICAN, by decision, and pinned by a test.
   *
   * Every listed US equity option -- which is every contract the option chain
   * beside this panel quotes -- is American-style. A panel that opens on
   * European opens on the one style the instrument in the ticket is NOT, and
   * the segment is the first thing a trader reads as "what am I pricing".
   *
   * This does NOT contradict `assistant.proto`'s extractor, which defaults to
   * EUROPEAN on an utterance that never says a style. That default answers a
   * different question: silence in a trader's own words is not evidence they
   * meant American, so the extractor refuses to infer one. Here there is no
   * utterance to be silent -- there is a listed contract, and it is American.
   */
  exerciseType: 'AMERICAN',
  bermudanDates: [],

  asianExpanded: false,
  asianType: 'NOT_ASIAN',
  averagingStates: DEFAULT_AVERAGING_STATES,

  steps: DEFAULT_STEPS,
  advancedOpen: false,

  loading: false,
  error: null,
  notReady: null,
  gateDenied: null,
  results: [],
  droppedDateCount: 0,

  setExerciseType: (style) => {
    set({ exerciseType: style });
    get().priceTree();
  },

  addBermudanPreset: (preset, yearsToExpiry) => {
    if (yearsToExpiry <= 0) return;
    // Fractions of the option's own remaining life, not calendar days: a
    // "quarterly" Bermudan on a 6-month option means two dates spaced at
    // T/2, not two 91-day calendar quarters that would overrun T.
    const count = preset === 'MONTHLY' ? 12 : preset === 'QUARTERLY' ? 4 : 2;
    const generated: BermudanDate[] = Array.from({ length: count }, (_, i) => {
      const yearFraction = ((i + 1) / count) * yearsToExpiry;
      return {
        id: Math.random().toString(36).slice(2),
        days: Math.round(yearFraction * 365),
        yearFraction,
      };
    });
    set((st) => ({ bermudanDates: [...st.bermudanDates, ...generated] }));
    get().priceTree();
  },

  addBermudanDay: (day, yearsToExpiry) => {
    if (day <= 0 || yearsToExpiry <= 0) return;
    const yearFraction = day / 365;
    if (yearFraction > yearsToExpiry) return;
    // Light entry-time dedupe so the chip list itself doesn't show two
    // chips for the same day; the closer-than-one-step dedupe still runs
    // again at request-build time against the actual tree step size.
    const exists = get().bermudanDates.some((d) => Math.abs(d.days - day) < 1);
    if (exists) return;
    set((st) => ({
      bermudanDates: [
        ...st.bermudanDates,
        { id: Math.random().toString(36).slice(2), days: day, yearFraction },
      ],
    }));
    get().priceTree();
  },

  removeBermudanDate: (id) => {
    set((st) => ({ bermudanDates: st.bermudanDates.filter((d) => d.id !== id) }));
    get().priceTree();
  },

  clearBermudanDates: () => {
    set({ bermudanDates: [] });
    get().priceTree();
  },

  setAsianExpanded: (expanded) => set({ asianExpanded: expanded }),

  setAsianType: (type) => {
    const prev = get().asianType;
    const enteringAsian = prev === 'NOT_ASIAN' && type !== 'NOT_ASIAN';
    const leavingAsian = prev !== 'NOT_ASIAN' && type === 'NOT_ASIAN';
    set((st) => ({
      asianType: type,
      // Lower the steps default to 60 on entry, restore 100 on exit -- but
      // only when the field is still sitting at the OTHER mode's default,
      // so a trader who deliberately typed their own value never has it
      // silently overwritten by a mode switch.
      steps: enteringAsian && st.steps === DEFAULT_STEPS
        ? DEFAULT_ASIAN_STEPS
        : leavingAsian && st.steps === DEFAULT_ASIAN_STEPS
          ? DEFAULT_STEPS
          : st.steps,
    }));
    get().priceTree();
  },

  setAveragingStates: (n) => {
    set({ averagingStates: Math.min(200, Math.max(10, Math.round(n) || 10)) });
    get().priceTree();
  },

  setSteps: (n) => {
    set({ steps: Math.max(2, Math.round(n) || 2) });
    get().priceTree();
  },

  setAdvancedOpen: (open) => set({ advancedOpen: open }),

  priceTree: () => {
    if (debounceTimer) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
      void executePricing(get, set);
    }, DEBOUNCE_MS);
  },
}));

type Getter = () => TreePricerState;
type Setter = (partial: Partial<TreePricerState>) => void;

async function executePricing(get: Getter, set: Setter): Promise<void> {
  const token = ++requestSeq;

  // Cleared above every early return, matching calculateStrategy: a denial
  // or error belongs to one attempt and must not outlive it.
  set({ gateDenied: null, notReady: null });

  const calc = useCalculatorStore.getState();
  const { ticket, spotPrice, symbol } = calc;

  if (spotPrice <= 0) {
    set({ results: [], notReady: `No spot price for ${symbol} yet -- pick a symbol with a live quote.` });
    return;
  }
  if (ticket.strike === null || ticket.strike <= 0) {
    set({ results: [], notReady: 'Pick a strike on the ticket to price exercise styles.' });
    return;
  }
  if (ticket.impliedVolatility === null || ticket.impliedVolatility <= 0) {
    set({
      results: [],
      notReady:
        'No implied volatility on the ticket. Pick a strike from the option chain so IV comes from a live quote.',
    });
    return;
  }
  const dte =
    calc.chainExpirations.find((e) => e.date === (ticket.expiration || calc.selectedExpiration))
      ?.dte ?? 0;
  if (dte <= 0) {
    set({ results: [], notReady: 'No expiry on the ticket -- pick one from the chain.' });
    return;
  }

  if (calc.rateSource === 'pending') {
    await calc.loadRiskFreeRate();
  }
  // Re-read after the await -- the fetch resolves into the store, not into
  // the destructured snapshot taken above.
  const rate = useCalculatorStore.getState().riskFreeRate;
  if (rate === null) {
    if (token !== requestSeq) return;
    set({
      results: [],
      error:
        useCalculatorStore.getState().rateSource === 'pending'
          ? 'Still fetching the Treasury rate -- retry in a moment.'
          : 'No risk-free rate -- the Treasury feed is unavailable. Enter a rate to proceed; it will be labelled an assumption.',
    });
    return;
  }

  const spot = spotPrice;
  const strike = ticket.strike;
  const vol = ticket.impliedVolatility;
  const optionType = ticket.optionType;
  const yearsToExpiry = dte / 365;

  const st = get();
  const isAsian = st.asianType !== 'NOT_ASIAN';
  const steps = clampSteps(st.steps, isAsian);
  const validBermudanDates = buildValidatedBermudanDates(st.bermudanDates, yearsToExpiry, steps);
  const droppedDateCount = st.bermudanDates.length - validBermudanDates.length;

  if (st.exerciseType === 'BERMUDAN' && isAsian && validBermudanDates.length === 0) {
    if (token !== requestSeq) return;
    set({
      results: [],
      notReady: 'Add at least one Bermudan exercise date to price this style.',
      droppedDateCount,
    });
    return;
  }

  set({ loading: true, error: null, notReady: null, droppedDateCount });

  try {
    // Dynamic import, inside the pricing action only: finance_pb.js is
    // 606 KB parsed / 31 KB gzipped against calculator_pb's 174 KB / 12 KB.
    // Static-importing it would put that cost on first paint for a panel
    // most users never open.
    const [{ FinanceClient }, financePb] = await Promise.all([
      import('../grpc/FinanceServiceClientPb'),
      import('../grpc/finance_pb'),
    ]);

    const backendUrl =
      process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
    const client = new FinanceClient(backendUrl);
    const metadata = (await import('../lib/licence')).authMetadata();

    const wireOptionType = optionType === 'CALL' ? WIRE_OPTION_TYPE.CALL : WIRE_OPTION_TYPE.PUT;

    const priceOne = async (
      style: ExerciseStyle,
      asianType: AsianStyle,
      averagingStates: number,
    ): Promise<TreePriceResult> => {
      const req = new financePb.OptionTreeRequest();
      req.setSpot(spot);
      req.setStrike(strike);
      req.setRate(rate as number);
      req.setVolatility(vol);
      req.setYearsToExpiry(yearsToExpiry);
      req.setSteps(steps);
      req.setOptionType(wireOptionType);
      req.setExerciseType(
        style === 'EUROPEAN'
          ? WIRE_EXERCISE_TYPE.EUROPEAN
          : style === 'AMERICAN'
            ? WIRE_EXERCISE_TYPE.AMERICAN
            : WIRE_EXERCISE_TYPE.BERMUDAN,
      );
      if (style === 'BERMUDAN') req.setBermudanDatesList(validBermudanDates);
      req.setAsianType(
        asianType === 'NOT_ASIAN'
          ? WIRE_ASIAN_TYPE.NOT_ASIAN
          : asianType === 'AVERAGE_PRICE'
            ? WIRE_ASIAN_TYPE.AVERAGE_PRICE
            : WIRE_ASIAN_TYPE.AVERAGE_STRIKE,
      );
      if (asianType !== 'NOT_ASIAN') req.setAveragingStates(averagingStates);
      // lambda is intentionally never set. The kernel silently rewrites it
      // when the resulting transition probabilities go negative
      // (options.cppm:127-133), so a value this UI sent could be silently
      // ignored -- exposing a control the engine may override is a lie.
      // Leaving it at the wire default (0) takes the engine's own default.

      const res = await client.priceOptionTree(req, metadata);
      return {
        style,
        value: res.getValue(),
        // Keyed on the REQUEST, never the response: an Asian response's
        // greeks are a structural {0,0,0}, byte-identical on the wire to a
        // genuine near-zero Greek.
        greeks:
          asianType === 'NOT_ASIAN'
            ? { delta: res.getDelta(), gamma: res.getGamma(), theta: res.getTheta() }
            : null,
        earlyExercisePremium: null,
      };
    };

    let results: TreePriceResult[];

    if (isAsian) {
      const clampedStates = clampAveragingStates(st.averagingStates);
      const result = await priceOne(st.exerciseType, st.asianType, clampedStates);
      results = [result];
    } else {
      const jobs: Promise<TreePriceResult>[] = [
        priceOne('EUROPEAN', 'NOT_ASIAN', 0),
        priceOne('AMERICAN', 'NOT_ASIAN', 0),
      ];
      if (validBermudanDates.length > 0) {
        jobs.push(priceOne('BERMUDAN', 'NOT_ASIAN', 0));
      }
      const settled = await Promise.all(jobs);
      const europeanValue = settled.find((r) => r.style === 'EUROPEAN')?.value ?? null;
      results = settled.map((r) =>
        europeanValue === null || r.style === 'EUROPEAN'
          ? r
          : { ...r, earlyExercisePremium: r.value - europeanValue },
      );
    }

    if (token !== requestSeq) return;
    set({ loading: false, error: null, notReady: null, results });
  } catch (err: unknown) {
    if (token !== requestSeq) return;
    const message = (err as Error).message || 'Tree pricing failed';
    const code = (err as { code?: number }).code;

    // PERMISSION_DENIED (7): the Pro upgrade path. Finance RPCs are
    // ungated, so this should never actually fire here -- handled anyway,
    // matched on the code and never the message text, which was reworded
    // twice in one day on the sibling strategy surface.
    if (code === RPC_PERMISSION_DENIED) {
      set({ loading: false, results: [], error: null, gateDenied: message });
      return;
    }
    // RESOURCE_EXHAUSTED (8): an ordinary, retryable rate limit. Paying
    // does not fix it, so it must never route through the upgrade prompt.
    if (code === RPC_RESOURCE_EXHAUSTED) {
      set({ loading: false, results: [], error: message, gateDenied: null });
      return;
    }
    set({ loading: false, results: [], error: message, gateDenied: null });
  }
}

export default useTreePricerStore;
