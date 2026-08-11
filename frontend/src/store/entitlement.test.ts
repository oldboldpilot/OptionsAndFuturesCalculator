/**
 * Pro-gate refusals must be discriminated by gRPC STATUS CODE, never by text.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The engine refuses a multi-leg position from an unentitled caller with
 * PERMISSION_DENIED (7). The UI has two entirely different renderings for a
 * failed calculation: `gateDenied` drives `UpgradePrompt` -- heading "Needs
 * Pro", the engine's own sentence, and the checkout buttons -- while `error`
 * drives the "Unavailable" branch in loss red. Sending an entitlement refusal
 * down the error branch tells a would-be subscriber their calculator is broken
 * instead of offering them the product, which is what it did until 2026-08-06.
 *
 * The discrimination is on the CODE because the MESSAGE is not stable: the
 * gate's copy was reworded twice in a single day. Any test that asserts on
 * message text would have to be rewritten each time the copy changes, and --
 * worse -- a store that matched on text would silently fall through to the
 * error branch the moment someone improved a sentence. These tests therefore
 * deliberately vary the message while holding the code fixed, and assert the
 * routing does not move.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { createFakeClient, strategyResponse, settle, GRPC_PERMISSION_DENIED } from '../test/grpc-harness';

/** Read at call time so each test can choose the outcome of the next RPC. */
let nextOutcome: { ok: ReturnType<typeof strategyResponse> } | { fail: { code: number; message: string } } = {
  ok: strategyResponse(),
};

const fake = createFakeClient({
  calculateStrategy: () => nextOutcome,
});

vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    getMarketQuote = fake.client.getMarketQuote;
    getRiskFreeRate = fake.client.getRiskFreeRate;
    getMarketChain = fake.client.getMarketChain;
    calculateStrategy = fake.client.calculateStrategy;
  },
}));

vi.mock('../lib/supabase/client', () => ({
  createClient: () => ({
    auth: {
      getSession: async () => ({ data: { session: null }, error: null }),
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
    },
  }),
}));

vi.mock('../lib/licence', () => ({ authMetadata: () => ({}) }));

import { useCalculatorStore } from './useCalculatorStore';

const leg = {
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 580,
  premium: 7.25,
  quantity: 1,
  expiration_days: 30,
  implied_volatility: 0.2,
};

/** Every precondition satisfied, so the only thing under test is the response. */
function readyToCalculate() {
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    legs: [leg, { ...leg, strike_price: 600, action: 'SELL' }],
    spotPrice: 580,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    gateDenied: null,
    isLoading: false,
  });
}

describe('entitlement refusals', () => {
  beforeEach(() => {
    nextOutcome = { ok: strategyResponse() };
    readyToCalculate();
  });

  it('routes PERMISSION_DENIED to the upgrade path, not the error path', async () => {
    nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Multi-leg strategies need Pro.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const st = useCalculatorStore.getState();
    expect(st.gateDenied).toBe('Multi-leg strategies need Pro.');
    // Critical: `error` must stay null, or the UI renders BOTH the upgrade
    // prompt and a red failure for the same refusal.
    expect(st.error).toBeNull();
    expect(st.result).toBeNull();
  });

  it('keeps routing on the code when the message is reworded', async () => {
    // The same refusal, three different sentences. This is not hypothetical --
    // the gate's copy changed twice in one day. If the store ever matches on
    // text, exactly one of these keeps working and the others fall through to
    // the red error branch.
    for (const message of [
      'Multi-leg strategies need Pro.',
      'This position needs a Pro subscription.',
      'Upgrade to price spreads.',
    ]) {
      readyToCalculate();
      nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message } };
      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.gateDenied).toBe(message);
      expect(st.error).toBeNull();
    }
  });

  it('routes every OTHER failure to the error path, not the upgrade path', async () => {
    // A dead backend is not a sales opportunity. Offering checkout when the
    // engine is unreachable takes money for something that cannot run.
    for (const code of [2 /* UNKNOWN */, 3 /* INVALID_ARGUMENT */, 14 /* UNAVAILABLE */]) {
      readyToCalculate();
      nextOutcome = { fail: { code, message: `failure with code ${code}` } };
      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.gateDenied).toBeNull();
      expect(st.error).toBe(`failure with code ${code}`);
    }
  });

  it('clears a previous gate refusal once the caller becomes entitled', async () => {
    nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Multi-leg strategies need Pro.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().gateDenied !== null, 'denied');

    // Now the same position succeeds -- a licence was entered, or a sign-in
    // completed. A stale upgrade prompt sitting above a correct answer would
    // tell a paying subscriber they still have not paid.
    readyToCalculate();
    nextOutcome = { ok: strategyResponse({ maxProfit: 1275, maxLoss: -725, breakEven: 587.25 }) };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().result !== null, 'result');

    const st = useCalculatorStore.getState();
    expect(st.gateDenied).toBeNull();
    expect(st.error).toBeNull();
    // The closed-form answer for a 580/600 bull call spread at a 7.25 debit,
    // the same figures the production gate check uses.
    expect(st.result?.max_profit).toBeCloseTo(1275, 2);
    expect(st.result?.max_loss).toBeCloseTo(-725, 2);
    expect(st.result?.break_even).toBeCloseTo(587.25, 2);
  });

  it('leaves isLoading false after a refusal', async () => {
    // A spinner that never stops is how a refused calculation looks like a hang.
    nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro.' } };
    await useCalculatorStore.getState().calculateStrategy();
    expect(useCalculatorStore.getState().isLoading).toBe(false);
  });
});
