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
/** The engine's "well-formed position, outside what this model covers" code. */
export const GRPC_FAILED_PRECONDITION = 9;
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
  /** `calculator.assistant.StrategyAssistant/ParseStrategy`. Promise-style. */
  parseStrategy?: (req: unknown) => RpcOutcome<FakeParseResponse>;
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

/* ---------------------------------------------------------------------------
   The strategy assistant.

   `ParseResponse` is a oneof whose THREE branches all arrive with gRPC status
   OK -- parameters, a clarifying question, and a refusal. The builders below
   are therefore three separate functions rather than one with optional fields:
   a builder that could produce two branches at once, or none, would model a
   response the service cannot send, and the store's discrimination would then
   be tested against a shape it will never meet.

   `getOutcomeCase()` is set by each builder to the oneof's own field number,
   which is what the generated message returns. Hand-built for the same reason
   everything else here is: a generated `ParseResponse` answers every accessor
   with a plausible zero, so a store reading the wrong branch would read an
   empty `StrategyParams` -- symbol "", expiry 0 -- and present it as a parse.
   -------------------------------------------------------------------------- */

export const PARSE_OUTCOME_NOT_SET = 0;
export const PARSE_OUTCOME_PARAMS = 1;
export const PARSE_OUTCOME_CLARIFICATION = 2;
export const PARSE_OUTCOME_REFUSAL = 3;

/** `Refusal.Reason`, by wire value. */
export const REFUSAL_UNSUPPORTED_STRATEGY = 1;
export const REFUSAL_UNKNOWN_SYMBOL = 2;
export const REFUSAL_OUT_OF_SCOPE = 3;
export const REFUSAL_MODEL_UNAVAILABLE = 4;
export const REFUSAL_DATA_UNAVAILABLE = 5;

export interface FakeStrategyParams {
  getSymbol(): string;
  getAssetClass(): string;
  getStrategy(): string;
  getExpirationDays(): number;
  getQuantity(): number;
  getFarExpirationDays(): number;
  getExerciseType(): number;
  getAsianType(): number;
}

export interface FakeClarification {
  getQuestion(): string;
}

export interface FakeRefusal {
  getReason(): number;
  getMessage(): string;
}

export interface FakeParseResponse {
  getOutcomeCase(): number;
  getParams(): FakeStrategyParams | undefined;
  getClarification(): FakeClarification | undefined;
  getRefusal(): FakeRefusal | undefined;
}

/**
 * A successful parse.
 *
 * `symbol`, `assetClass`, `strategy`, `expirationDays` and `quantity` are all
 * REQUIRED. None of them has a defensible default: an expiry of 0 is exactly
 * the fabricated value the ticket defect turned on, and an asset class picked
 * for the caller is how a futures root gets priced off an equity quote.
 *
 * `farExpirationDays`, `exerciseType` and `asianType` default to 0 because
 * `assistant.proto` gives 0 a stated meaning for each -- "not stated",
 * EUROPEAN and NOT_ASIAN respectively -- so the default here is the contract's
 * own, not this builder's invention.
 */
export function parseParams(p: {
  symbol: string;
  assetClass: string;
  strategy: string;
  expirationDays: number;
  quantity: number;
  farExpirationDays?: number;
  exerciseType?: number;
  asianType?: number;
}): FakeParseResponse {
  const params: FakeStrategyParams = {
    getSymbol: () => p.symbol,
    getAssetClass: () => p.assetClass,
    getStrategy: () => p.strategy,
    getExpirationDays: () => p.expirationDays,
    getQuantity: () => p.quantity,
    getFarExpirationDays: () => p.farExpirationDays ?? 0,
    getExerciseType: () => p.exerciseType ?? 0,
    getAsianType: () => p.asianType ?? 0,
  };
  return {
    getOutcomeCase: () => PARSE_OUTCOME_PARAMS,
    getParams: () => params,
    getClarification: () => undefined,
    getRefusal: () => undefined,
  };
}

/** A clarifying question. gRPC status OK -- the model doing its job. */
export function parseClarification(question: string): FakeParseResponse {
  return {
    getOutcomeCase: () => PARSE_OUTCOME_CLARIFICATION,
    getParams: () => undefined,
    getClarification: () => ({ getQuestion: () => question }),
    getRefusal: () => undefined,
  };
}

/** A refusal. Also gRPC status OK, and also the model doing its job. */
export function parseRefusal(reason: number, message: string): FakeParseResponse {
  return {
    getOutcomeCase: () => PARSE_OUTCOME_REFUSAL,
    getParams: () => undefined,
    getClarification: () => undefined,
    getRefusal: () => ({ getReason: () => reason, getMessage: () => message }),
  };
}

/**
 * A response with the oneof unset. Not a thing the service sends today, which
 * is precisely why the store must not read it as one of the three that it does.
 */
export function parseNothing(): FakeParseResponse {
  return {
    getOutcomeCase: () => PARSE_OUTCOME_NOT_SET,
    getParams: () => undefined,
    getClarification: () => undefined,
    getRefusal: () => undefined,
  };
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
      /** Promise-style, like calculateStrategy: the assistant store awaits it. */
      parseStrategy: (req: unknown) => {
        calls.push({ method: 'parseStrategy', request: req });
        const handler = script.parseStrategy;
        if (!handler) {
          return Promise.reject({ code: GRPC_UNAVAILABLE, message: 'no parseStrategy handler scripted in this test' });
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
