/**
 * The refusal ladder in `calculate()`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Every guard here exists because the store once answered a question it had
 * not been asked. Spot, volatility and the risk-free rate were all hardcoded
 * at some point ('SPY', 0.20, 30 days, 0.05), so the engine returned a real
 * number computed from a fabricated input, and nothing in the UI distinguished
 * that from a quote. The rule the guards enforce is: refuse rather than
 * substitute.
 *
 * These tests pin the ORDER as well as the conditions. Order is load-bearing:
 * a position with no spot AND no rate must complain about spot, because that is
 * the first thing the user can act on, and a refusal that names the wrong
 * missing input sends them to fix something that was never wrong.
 *
 * Two of the messages assert a distinction rather than a failure, and those are
 * the ones worth the most. Telling a user to "pick an expiry from the chain"
 * when they just picked a same-day expiry, or to "add legs from the option
 * chain" when the legs are already there, denies what they just did -- which is
 * how a correct refusal still reads as a broken product.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  rateResponse,
  strategyResponse,
  settle,
} from '../test/grpc-harness';

/**
 * Scripted so the rate can be varied per test: `rateOutcome` is read at call
 * time, not captured at module load, so a test can make the Treasury feed fail
 * without rebuilding the client.
 */
let rateFails = false;

const fake = createFakeClient({
  getMarketChain: () => ({
    ok: chainResponse(
      [chainExpiration('2026-08-10', 0), chainExpiration('2026-08-17', 7)],
      [chainStrike(773, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.101 })],
      '2026-08-17'
    ),
  }),
  getRiskFreeRate: () =>
    rateFails
      ? { fail: { code: 14, message: 'treasury feed unavailable' } }
      : { ok: rateResponse(0.0385) },
  calculateStrategy: () => ({ ok: strategyResponse({ maxProfit: 18869.75, maxLoss: -459, breakEven: 777.59 }) }),
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

/** A priced, well-formed leg. Tests remove exactly one property to isolate a guard. */
function goodLeg(over: Partial<Record<string, unknown>> = {}) {
  return {
    instrument_type: 'INSTRUMENT_EQUITY_OPTION',
    action: 'BUY',
    option_type: 'CALL',
    strike_price: 773,
    premium: 4.59,
    quantity: 1,
    expiration_days: 7,
    implied_volatility: 0.101,
    ...over,
  };
}

/** Reset to the store's declared initial state between tests. */
function reset(over: Record<string, unknown> = {}) {
  rateFails = false;
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    legs: [],
    spotPrice: 773.03,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    gateDenied: null,
    isLoading: false,
    ...over,
  });
}

describe('calculate() refusal ladder', () => {
  beforeEach(() => reset());

  it('clears rather than refusing when there are no legs', async () => {
    await useCalculatorStore.getState().calculateStrategy();
    const st = useCalculatorStore.getState();
    // An empty position is not an error state -- it is the starting state.
    // Showing a refusal here would greet every first-time visitor with red text.
    expect(st.error).toBeNull();
    expect(st.result).toBeNull();
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
  });

  it('refuses without a spot price, naming the symbol', async () => {
    reset({ legs: [goodLeg()], spotPrice: 0, symbol: 'QQQ' });
    await useCalculatorStore.getState().calculateStrategy();
    const st = useCalculatorStore.getState();
    expect(st.error).toBe('No spot price for QQQ — cannot price the position.');
    expect(st.result).toBeNull();
    // The engine must not be asked a question built on a missing input.
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
  });

  it('checks spot BEFORE volatility, so the refusal names the first fixable input', async () => {
    reset({ legs: [goodLeg({ implied_volatility: undefined })], spotPrice: 0 });
    await useCalculatorStore.getState().calculateStrategy();
    // Both are missing. Naming IV here would send the user to the chain when
    // the actual blocker is the quote feed.
    expect(useCalculatorStore.getState().error).toContain('No spot price');
  });

  describe('implied volatility', () => {
    /*
     * There is no test here for the "add legs from the option chain" branch.
     *
     * It is UNREACHABLE through `calculateStrategy`: the `legs.length === 0`
     * early return above it fires first, so a position with no legs never gets
     * as far as the IV check. An earlier version of this file "covered" it by
     * setting legs to [] and asserting `error` is null -- which passes for the
     * wrong reason, duplicating the no-legs test while its comment claimed to
     * be pinning an IV distinction it never exercised. A test that passes for a
     * reason other than the one it states is worse than no test: it reports
     * coverage that does not exist.
     *
     * The branch is kept in the store as correct advice should the guard order
     * ever change. If it does, this comment is the note that it now needs one.
     */
    it('does NOT tell a populated position to add legs it already has', async () => {
      reset({ legs: [goodLeg({ implied_volatility: undefined })] });
      await useCalculatorStore.getState().calculateStrategy();
      const err = useCalculatorStore.getState().error ?? '';

      // The regression this pins: the message used to be "Add legs from the
      // option chain so IV and premium come from live quotes" for BOTH cases.
      // A user who had just added a same-day-expiry leg from the chain was told
      // to go and do the thing they had done -- while the actual remedy, the
      // ticket's own IV field, went unmentioned.
      expect(err).not.toContain('Add legs from the option chain');
      expect(err).toContain('publish no implied volatility');
      expect(err).toContain('Enter an IV in the ticket');
      expect(useCalculatorStore.getState().result).toBeNull();
    });
  });

  describe('time to expiry', () => {
    it('distinguishes a same-day expiry from a missing one', async () => {
      // dte 0 is a CHOICE: the chain lists a same-day expiration and picking it
      // is legitimate. Saying "no expiration on any leg" denies that choice.
      reset({ legs: [goodLeg({ expiration_days: 0 })] });
      await useCalculatorStore.getState().calculateStrategy();
      const err = useCalculatorStore.getState().error ?? '';
      expect(err).toContain('Every leg expires today');
      expect(err).not.toContain('No expiration on any leg');
    });

    it('still says "no expiration" when the field is genuinely absent', async () => {
      reset({ legs: [goodLeg({ expiration_days: undefined })] });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().error).toBe(
        'No expiration on any leg — pick an expiry from the chain.'
      );
    });

    it('takes the LONGEST expiry across legs, not the shortest', async () => {
      // A calendar spread has a near leg and a far leg. Reducing the horizon to
      // the near one would truncate the payoff curve of every strategy that
      // holds a longer-dated leg.
      reset({
        legs: [goodLeg({ expiration_days: 0 }), goodLeg({ expiration_days: 30 })],
      });
      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().result !== null, 'result');
      expect(useCalculatorStore.getState().error).toBeNull();
    });
  });

  describe('risk-free rate', () => {
    it('fetches the rate on demand rather than assuming one', async () => {
      reset({ legs: [goodLeg()], riskFreeRate: null, rateSource: 'pending' });
      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().result !== null, 'result');

      expect(fake.calls.some((c) => c.method === 'getRiskFreeRate')).toBe(true);
      expect(useCalculatorStore.getState().riskFreeRate).toBeCloseTo(0.0385, 6);
    });

    it('refuses, without inventing a rate, when the Treasury feed fails', async () => {
      rateFails = true;
      useCalculatorStore.setState({
        legs: [goodLeg()],
        spotPrice: 773.03,
        riskFreeRate: null,
        rateSource: 'pending',
        result: null,
        error: null,
      });
      await useCalculatorStore.getState().calculateStrategy();

      const st = useCalculatorStore.getState();
      // The old behaviour was a silent 0.05, which then sat behind probability
      // of profit and expected value looking exactly like a quoted figure.
      expect(st.riskFreeRate).toBeNull();
      expect(st.result).toBeNull();
      expect(st.error).toContain('Treasury feed is unavailable');
      expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
    });
  });

  it('reaches the engine and stores the result once every precondition holds', async () => {
    reset({ legs: [goodLeg()] });
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().result !== null, 'result');

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.isLoading).toBe(false);
    expect(st.result?.max_profit).toBeCloseTo(18869.75, 2);
    expect(st.result?.max_loss).toBeCloseTo(-459, 2);
    expect(st.result?.break_even).toBeCloseTo(777.59, 2);
  });
});
