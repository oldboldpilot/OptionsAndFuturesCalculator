/**
 * A fake transport for the generated gRPC-Web client, plus builders for the
 * response objects the stores read.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The stores talk to `OptionsCalculatorClient` and nothing else reaches the
 * network, so this file is the whole seam. Everything below the seam -- the
 * generated request classes, zustand, the guards -- stays real, because those
 * are the parts under test.
 *
 * Responses are plain objects carrying the getter METHODS the store actually
 * calls, not instances of the generated classes. Two reasons. Constructing a
 * real `StrategyResponse` means setting thirty fields through setters to
 * exercise one guard, and a test that verbose stops being read. More
 * importantly, a hand-built response makes a MISSING field explicit: if the
 * store starts calling a getter no builder provides, the test fails with
 * "not a function" naming it, rather than silently reading a zero the
 * generated default would have supplied. A default that looks like an answer
 * is precisely the failure mode this suite exists to catch.
 */
// (no vitest import needed: this file builds data, it does not mock)

/** gRPC status codes the stores discriminate on. Fixed by the spec. */
export const GRPC_OK = 0;
export const GRPC_PERMISSION_DENIED = 7;
export const GRPC_UNAVAILABLE = 14;

/** A gRPC-Web error as the generated client surfaces it to a callback. */
export interface FakeRpcError {
  code: number;
  message: string;
}

/**
 * What a single RPC should do when called.
 *
 * `ok` resolves with the response; `fail` invokes the error path. A handler is
 * a function of the request so a test can vary the answer by argument -- the
 * chain, for instance, returns different strikes per expiration.
 */
export type RpcOutcome<T> = { ok: T } | { fail: FakeRpcError };

export interface RpcScript {
  getMarketQuote?: (req: unknown) => RpcOutcome<FakeQuoteResponse>;
  getRiskFreeRate?: (req: unknown) => RpcOutcome<FakeRateResponse>;
  getMarketChain?: (req: unknown) => RpcOutcome<FakeChainResponse>;
  calculateStrategy?: (req: unknown) => RpcOutcome<FakeStrategyResponse>;
}

/** Calls recorded by the fake, so a test can assert what was SENT, not only what came back. */
export interface RpcCallLog {
  method: keyof RpcScript;
  request: unknown;
}

export interface FakeQuoteResponse {
  getPrice(): number;
}

export interface FakeRateResponse {
  getRate(): number;
  getTenor(): string;
  getAsOfDate(): string;
  getSource(): string;
  getRatePublished(): number;
  getFetchedAt(): string;
}

export interface FakeChainExpiration {
  getDateStr(): string;
  getDaysToExpiry(): number;
  getLabel(): string;
}

export interface FakeChainStrike {
  getStrike(): number;
  getIsAtm(): boolean;
  getCallBid(): number;
  getCallAsk(): number;
  getCallDelta(): number;
  getCallIv(): number;
  getCallVolume(): number;
  getCallOpenInterest(): number;
  getPutBid(): number;
  getPutAsk(): number;
  getPutDelta(): number;
  getPutIv(): number;
  getPutVolume(): number;
  getPutOpenInterest(): number;
}

export interface FakeChainResponse {
  getAvailableExpirationsList(): FakeChainExpiration[];
  getOptionStrikesList(): FakeChainStrike[];
  getFuturesContractsList(): unknown[];
  getSelectedExpirationDate(): string;
}

export interface FakeStrategyResponse {
  getMaxProfit(): number;
  getMaxLoss(): number;
  getBreakEven(): number;
  getExpectedValue(): number;
  getPop(): number;
  getCurveDaysToExpiration(): number;
  getMatrixList(): unknown[];
  getPnlMatrixList(): unknown[];
  getLegRiskList(): unknown[];
  getNetGreeks(): unknown;
  getRiskMetrics(): unknown;
}

/**
 * Build a chain expiration. `dte` is deliberately required: the defect that
 * prompted this suite was an expiration that reached a leg carrying a
 * fabricated zero, so no builder here is allowed to invent one.
 */
export function chainExpiration(date: string, dte: number, label = date): FakeChainExpiration {
  return {
    getDateStr: () => date,
    getDaysToExpiry: () => dte,
    getLabel: () => label,
  };
}

/**
 * Build a chain strike. `iv` accepts 0 to model a contract the feed publishes
 * no implied volatility for -- ordinary at a same-day expiry, and its own
 * branch in the store's refusal ladder.
 */
export function chainStrike(
  strike: number,
  opts: {
    isAtm?: boolean;
    callBid?: number;
    callAsk?: number;
    callIv?: number;
    callDelta?: number;
    callVolume?: number;
    callOpenInterest?: number;
    putBid?: number;
    putAsk?: number;
    putIv?: number;
    putDelta?: number;
    putVolume?: number;
    putOpenInterest?: number;
  } = {}
): FakeChainStrike {
  const {
    isAtm = false,
    callBid = 1,
    callAsk = 1.2,
    callIv = 0.2,
    callDelta = 0.5,
    callVolume = 100,
    callOpenInterest = 500,
    // The put side defaults MIRROR the call side, which is convenient and
    // dangerous in equal measure: a test built on the defaults cannot detect a
    // transposed call/put field, because both sides carry the same numbers. Any
    // test asserting field FIDELITY must pass disjoint values on every field
    // below -- see the `distinctStrike` helper in chain.test.ts, which exists
    // because this builder alone would have let a transposition pass.
    putBid = 1,
    putAsk = 1.2,
    putIv = callIv,
    putDelta = -0.5,
    putVolume = 100,
    putOpenInterest = 500,
  } = opts;
  return {
    getStrike: () => strike,
    getIsAtm: () => isAtm,
    getCallBid: () => callBid,
    getCallAsk: () => callAsk,
    getCallDelta: () => callDelta,
    getCallIv: () => callIv,
    getCallVolume: () => callVolume,
    getCallOpenInterest: () => callOpenInterest,
    getPutBid: () => putBid,
    getPutAsk: () => putAsk,
    getPutDelta: () => putDelta,
    getPutIv: () => putIv,
    getPutVolume: () => putVolume,
    getPutOpenInterest: () => putOpenInterest,
  };
}

export function chainResponse(
  expirations: FakeChainExpiration[],
  strikes: FakeChainStrike[],
  selected = '',
  futures: unknown[] = []
): FakeChainResponse {
  return {
    getAvailableExpirationsList: () => expirations,
    getOptionStrikesList: () => strikes,
    getFuturesContractsList: () => futures,
    getSelectedExpirationDate: () => selected,
  };
}

export function quoteResponse(price: number): FakeQuoteResponse {
  return { getPrice: () => price };
}

export function rateResponse(rate: number, opts: { tenor?: string; asOf?: string; source?: string } = {}): FakeRateResponse {
  const { tenor = '3M', asOf = '2026-08-10', source = 'us_treasury_par_yield' } = opts;
  return {
    getRate: () => rate,
    getTenor: () => tenor,
    getAsOfDate: () => asOf,
    getSource: () => source,
    getRatePublished: () => rate,
    getFetchedAt: () => '2026-08-10T00:00:00Z',
  };
}

export function strategyResponse(
  over: Partial<{ maxProfit: number; maxLoss: number; breakEven: number; expectedValue: number; pop: number; curveDays: number }> = {}
): FakeStrategyResponse {
  const { maxProfit = 1000, maxLoss = -500, breakEven = 100, expectedValue = 0, pop = 0.5, curveDays = 30 } = over;
  return {
    getMaxProfit: () => maxProfit,
    getMaxLoss: () => maxLoss,
    getBreakEven: () => breakEven,
    getExpectedValue: () => expectedValue,
    getPop: () => pop,
    getCurveDaysToExpiration: () => curveDays,
    getMatrixList: () => [],
    getPnlMatrixList: () => [],
    getLegRiskList: () => [],
    getNetGreeks: () => undefined,
    getRiskMetrics: () => undefined,
  };
}

/**
 * Install the fake client.
 *
 * Call from a `vi.mock` factory for '../grpc/CalculatorServiceClientPb'. The
 * returned log is live: assertions can read it after the store settles.
 */
export function createFakeClient(script: RpcScript) {
  const calls: RpcCallLog[] = [];

  /** Callback-style RPCs: (req, metadata, cb) => cb(err, res). */
  const callbackRpc =
    (method: keyof RpcScript) =>
    (req: unknown, _meta: unknown, cb: (err: FakeRpcError | null, res: unknown) => void) => {
      calls.push({ method, request: req });
      const handler = script[method];
      if (!handler) {
        // An unscripted call is a test authoring error, not a store defect.
        // Say so loudly rather than handing back an empty success.
        cb({ code: GRPC_UNAVAILABLE, message: `no ${method} handler scripted in this test` }, null);
        return;
      }
      const outcome = handler(req);
      if ('fail' in outcome) cb(outcome.fail, null);
      else cb(null, outcome.ok);
    };

  return {
    calls,
    client: {
      getMarketQuote: callbackRpc('getMarketQuote'),
      getRiskFreeRate: callbackRpc('getRiskFreeRate'),
      getMarketChain: callbackRpc('getMarketChain'),
      /** Promise-style: the store awaits this one. */
      calculateStrategy: (req: unknown) => {
        calls.push({ method: 'calculateStrategy', request: req });
        const handler = script.calculateStrategy;
        if (!handler) {
          return Promise.reject({ code: GRPC_UNAVAILABLE, message: 'no calculateStrategy handler scripted in this test' });
        }
        const outcome = handler(req);
        return 'fail' in outcome ? Promise.reject(outcome.fail) : Promise.resolve(outcome.ok);
      },
    },
  };
}

/**
 * Wait for the store to settle.
 *
 * The callback RPCs resolve synchronously here, but the store awaits between
 * them (the rate fetch before the strategy call), so a test must yield the
 * microtask queue rather than assert immediately. Polling a predicate keeps a
 * test from depending on how many awaits deep the implementation happens to be.
 */
export async function settle(predicate: () => boolean, label = 'condition', tries = 50): Promise<void> {
  for (let i = 0; i < tries; i += 1) {
    if (predicate()) return;
    await new Promise((r) => setTimeout(r, 1));
  }
  throw new Error(`settle: ${label} never became true after ${tries} ticks`);
}

/*
 * There is deliberately NO `mockAmbient()` helper here.
 *
 * `vi.mock` is hoisted to the top of the module it appears in, so wrapping the
 * calls in a function that a test then invokes does not do what it reads like:
 * the mocks would apply to THIS file, before any test runs, and the call site
 * would be decorative. Vitest warns about it today and will make it an error.
 *
 * Each test file therefore declares its own `vi.mock` blocks at top level --
 * see `harness.canary.test.ts` for the shape to copy. The duplication is a few
 * lines per file and it keeps the hoisting visible where it actually happens.
 */
