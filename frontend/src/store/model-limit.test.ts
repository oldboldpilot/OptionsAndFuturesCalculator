/**
 * A modelling limit is not an outage, and must not render as one.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The engine refuses a position it cannot describe -- today, one containing an
 * Asian leg, whose payoff is a function of the AVERAGE price rather than the
 * price at expiry -- with FAILED_PRECONDITION (9). Nothing is broken and
 * retrying will not help, so it belongs in its own state rather than in
 * `error`, which drives the "Unavailable" branch in loss red and tells a
 * trader the calculator has fallen over.
 *
 * This is the third routing this store performs on a gRPC code, and all three
 * are pinned the same way: on the CODE, with the message deliberately varied,
 * because the copy is not stable. See entitlement.test.ts for the incident
 * that established the rule.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  strategyResponse,
  settle,
  GRPC_FAILED_PRECONDITION,
  GRPC_PERMISSION_DENIED,
  GRPC_UNAVAILABLE,
} from '../test/grpc-harness';

let nextOutcome:
  | { ok: ReturnType<typeof strategyResponse> }
  | { fail: { code: number; message: string } } = { ok: strategyResponse() };

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
  id: 'leg-model-1',
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 580,
  premium: 7.25,
  quantity: 1,
  expiration_days: 30,
  implied_volatility: 0.2,
};

function ready() {
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    legs: [leg],
    spotPrice: 590,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    gateDenied: null,
    modelLimit: null,
  });
}

describe('FAILED_PRECONDITION is a modelling limit, not an error', () => {
  beforeEach(() => {
    ready();
  });

  it('routes code 9 to modelLimit and leaves error clear', async () => {
    nextOutcome = {
      fail: {
        code: GRPC_FAILED_PRECONDITION,
        message:
          'This position contains an Asian option, which pays on the average price over its ' +
          'averaging window rather than the price at expiry.',
      },
    };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const s = useCalculatorStore.getState();
    expect(s.modelLimit).toContain('average price');
    expect(s.error).toBeNull();
    expect(s.gateDenied).toBeNull();
    expect(s.result).toBeNull();
  });

  // The message is varied on purpose. A store that matched text would pass the
  // test above and fail here, which is exactly the regression being prevented.
  it('routes code 9 regardless of wording', async () => {
    nextOutcome = {
      fail: { code: GRPC_FAILED_PRECONDITION, message: 'Averaging style not supported here.' },
    };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const s = useCalculatorStore.getState();
    expect(s.modelLimit).toBe('Averaging style not supported here.');
    expect(s.error).toBeNull();
  });

  // Break direction. Without these, "modelLimit is set" is satisfied by a
  // store that routes EVERY failure there -- which would swallow real outages
  // into a calm explanatory panel.
  it('does NOT route a genuine transport failure to modelLimit', async () => {
    nextOutcome = { fail: { code: GRPC_UNAVAILABLE, message: 'connection refused' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const s = useCalculatorStore.getState();
    expect(s.error).toBe('connection refused');
    expect(s.modelLimit).toBeNull();
  });

  it('does NOT route an entitlement refusal to modelLimit', async () => {
    nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const s = useCalculatorStore.getState();
    expect(s.gateDenied).toBe('Needs Pro.');
    expect(s.modelLimit).toBeNull();
    expect(s.error).toBeNull();
  });

  it('clears a stale modelLimit once the position calculates', async () => {
    nextOutcome = { fail: { code: GRPC_FAILED_PRECONDITION, message: 'Asian leg.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');
    expect(useCalculatorStore.getState().modelLimit).toBe('Asian leg.');

    nextOutcome = { ok: strategyResponse() };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const s = useCalculatorStore.getState();
    expect(s.modelLimit).toBeNull();
    expect(s.result).not.toBeNull();
  });
});
