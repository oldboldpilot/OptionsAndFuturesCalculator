/**
 * User-set price bounds on the P&L matrix.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The bounds are a window on ONE panel's data, and the reason that distinction
 * is worth a test file is the failure it avoids. The engine sweeps a single
 * price grid four times -- the expiry curve (which is where max profit, max
 * loss and breakeven come from), the breakeven interpolation, the matrix, and
 * the lognormal probability distribution. Applying a user's window to that
 * shared grid would have reported the best outcome INSIDE the window as the
 * position's maximum profit: correct arithmetic answering a question nobody
 * asked. `matrix_price_min`/`_max` therefore drive a grid of their own, and
 * section 7 of `test_calculator_service` pins that invariant on the engine.
 *
 * What is testable HERE is the half the engine cannot see: that the client
 * sends what the user typed, that an unset bound travels as the wire's own
 * zero sentinel rather than as a number, that clearing one bound leaves the
 * other alone, and that a value the engine would refuse never leaves the tab.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  quoteResponse,
  rateResponse,
  strategyResponse,
} from '../test/grpc-harness';

const fake = createFakeClient({
  getMarketQuote: () => ({ ok: quoteResponse(580) }),
  getRiskFreeRate: () => ({ ok: rateResponse(0.0385) }),
  calculateStrategy: () => ({ ok: strategyResponse() }),
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

import { useCalculatorStore, buildStrategyRequest } from './useCalculatorStore';

const leg = {
  id: 'leg-bounds-1',
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 580,
  premium: 4.5,
  quantity: 1,
  expiration_days: 30,
  implied_volatility: 0.2,
};

/** The last request the store actually put on the wire. */
function lastRequest() {
  const sent = fake.calls.filter((c) => c.method === 'calculateStrategy');
  return sent[sent.length - 1]?.request as
    | { getMatrixPriceMin(): number; getMatrixPriceMax(): number }
    | undefined;
}

beforeEach(() => {
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    assetClass: 'EQUITY',
    legs: [leg as never],
    spotPrice: 580,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    dividendYield: 0,
    matrixPriceMin: null,
    matrixPriceMax: null,
    result: null,
    isLoading: false,
    error: null,
    notReady: null,
    gateDenied: null,
    modelLimit: null,
  } as never);
});

describe('buildStrategyRequest carries the bounds', () => {
  const input = {
    symbol: 'SPY',
    spotPrice: 580,
    impliedVolatility: 0.2,
    riskFreeRate: 0.0385,
    dividendYield: 0,
    legs: [leg as never],
    horizonDays: 30,
  };

  it('sends zero on both sides when no bound is set', () => {
    // Zero is the wire's "unset", and it is stated rather than left to the
    // proto3 default so this function stays total over its input.
    const req = buildStrategyRequest(input);
    expect(req.getMatrixPriceMin()).toBe(0);
    expect(req.getMatrixPriceMax()).toBe(0);
  });

  it('sends each bound independently', () => {
    const only_hi = buildStrategyRequest({ ...input, matrixPriceMax: 700 });
    expect(only_hi.getMatrixPriceMin()).toBe(0);
    expect(only_hi.getMatrixPriceMax()).toBe(700);

    const only_lo = buildStrategyRequest({ ...input, matrixPriceMin: 500 });
    expect(only_lo.getMatrixPriceMin()).toBe(500);
    expect(only_lo.getMatrixPriceMax()).toBe(0);
  });
});

describe('setMatrixBounds', () => {
  it('puts the typed window on the wire and reprices', async () => {
    // The whole feature in one assertion: the user's numbers reach the engine
    // WITHOUT the caller having to ask for a recalculation.
    useCalculatorStore.getState().setMatrixBounds({ min: 480, max: 620 });
    await vi.waitFor(() => expect(lastRequest()).toBeDefined());

    expect(lastRequest()!.getMatrixPriceMin()).toBe(480);
    expect(lastRequest()!.getMatrixPriceMax()).toBe(620);
  });

  it('is a PATCH: clearing one bound leaves the other standing', async () => {
    useCalculatorStore.getState().setMatrixBounds({ min: 480, max: 620 });
    await vi.waitFor(() => expect(lastRequest()).toBeDefined());

    // `{ min: null }` says "unpin the floor". A pair-shaped setter would have
    // taken the ceiling down with it, silently widening the view the user was
    // reading.
    useCalculatorStore.getState().setMatrixBounds({ min: null });
    await vi.waitFor(() =>
      expect(useCalculatorStore.getState().matrixPriceMin).toBeNull(),
    );

    expect(useCalculatorStore.getState().matrixPriceMax).toBe(620);
    expect(lastRequest()!.getMatrixPriceMin()).toBe(0);
    expect(lastRequest()!.getMatrixPriceMax()).toBe(620);
  });

  it('an omitted key changes nothing', () => {
    useCalculatorStore.setState({ matrixPriceMin: 500, matrixPriceMax: 700 } as never);
    // `undefined` and `null` are DIFFERENT here -- "leave it" against "clear
    // it" -- which is exactly the distinction a pair of positional arguments
    // could not express.
    useCalculatorStore.getState().setMatrixBounds({});
    expect(useCalculatorStore.getState().matrixPriceMin).toBe(500);
    expect(useCalculatorStore.getState().matrixPriceMax).toBe(700);
  });

  it.each([
    ['zero', 0],
    ['negative', -10],
    ['NaN', Number.NaN],
    ['Infinity', Number.POSITIVE_INFINITY],
  ])('%s never leaves the tab', (_label, value) => {
    useCalculatorStore.getState().setMatrixBounds({ min: value, max: value });
    // Collapsed to "unset" rather than sent. The engine refuses every one of
    // these, and a refusal blanks the grid behind a red Unavailable -- the
    // wrong shape for a half-typed number in a field the user is looking at.
    expect(useCalculatorStore.getState().matrixPriceMin).toBeNull();
    expect(useCalculatorStore.getState().matrixPriceMax).toBeNull();
  });

  it('does NOT normalise an inverted pair', () => {
    // Deliberate. Two repairs are plausible -- swap them, or drop one -- and
    // choosing either in the client answers a question the user did not ask.
    // The engine refuses and names both numbers; the panel refuses to send it.
    useCalculatorStore.getState().setMatrixBounds({ min: 620, max: 480 });
    expect(useCalculatorStore.getState().matrixPriceMin).toBe(620);
    expect(useCalculatorStore.getState().matrixPriceMax).toBe(480);
  });
});
