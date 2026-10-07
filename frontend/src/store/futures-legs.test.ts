/**
 * A futures position is priced without an implied volatility (OFC-007).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Observed on production: pick ES, Futures Outright Long, Apply. The position
 * read `BUY FUTURE 7,855.19` and every panel answered "Not priced yet. These
 * contracts publish no implied volatility, common at a same-day expiry. Enter
 * an IV in the ticket, or choose a later expiration." -- advice that cannot be
 * followed, on the free single-leg path, for a December contract 72 days out,
 * with a ticket that has no futures mode and an IV box that belongs to options.
 *
 * A future has no volatility in its payoff. Its profit is (price - entry) x
 * multiplier x quantity; the engine computes that, the maximum, the break-even
 * and the whole matrix without a sigma and simply leaves the probability fields
 * at zero. The store's guard was written for OPTION legs and applied to
 * everything, so it refused a position the engine can price exactly.
 *
 * What stays refused: an option leg with no IV, which the engine cannot price,
 * and which already has its own wording.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  rateResponse,
  strategyResponse,
} from '../test/grpc-harness';

const fake = createFakeClient({
  getRiskFreeRate: () => ({ ok: rateResponse(0.0385) }),
  calculateStrategy: () => ({ ok: strategyResponse({ maxProfit: 5000, maxLoss: -5000, maxProfitUnbounded: true }) }),
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
import { futuresMultiplier } from '../config/futures';

const future = {
  instrument_type: 'INSTRUMENT_FUTURES_SPOT',
  action: 'BUY',
  option_type: 'FUTURE',
  strike_price: 7855.19,
  premium: 7855.19,
  quantity: 1,
  expiration_days: 72,
  contract_multiplier: 50,
};

const optionNoIv = {
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 7900,
  premium: 120,
  quantity: 1,
  expiration_days: 72,
};

function reset(over: Record<string, unknown> = {}) {
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'ES',
    assetClass: 'FUTURES',
    legs: [],
    spotPrice: 7790.9,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    notReady: null,
    gateDenied: null,
    modelLimit: null,
    isLoading: false,
    retiredPosition: null,
    ...over,
  });
}
const st = () => useCalculatorStore.getState();

describe('a futures-only position', () => {
  beforeEach(() => reset());

  it('is priced with no implied volatility anywhere in it', async () => {
    st().addLeg(future);
    await st().calculateStrategy();

    expect(st().notReady).toBeNull();
    expect(st().error).toBeNull();
    expect(st().result).not.toBeNull();
    expect(fake.calls.filter((c) => c.method === 'calculateStrategy')).toHaveLength(1);
  });

  it('tells the engine there is no volatility rather than inventing one', async () => {
    st().addLeg(future);
    await st().calculateStrategy();
    const sent = fake.calls.find((c) => c.method === 'calculateStrategy')!.request as {
      getImpliedVolatility(): number;
    };
    expect(sent.getImpliedVolatility()).toBe(0);
  });

  it('records that the volatility is absent so panels need not guess', async () => {
    st().addLeg(future);
    await st().calculateStrategy();
    expect(st().result!.inputs.impliedVolatility).toBe(0);
  });

  it('uses a stated volatility when the trader gives one', async () => {
    st().addLeg({ ...future, implied_volatility: 0.14 });
    await st().calculateStrategy();
    const sent = fake.calls.find((c) => c.method === 'calculateStrategy')!.request as {
      getImpliedVolatility(): number;
    };
    expect(sent.getImpliedVolatility()).toBeCloseTo(0.14, 10);
  });

  it('carries the "unbounded" verdict through and withholds the ratio', async () => {
    st().addLeg(future);
    await st().calculateStrategy();
    const r = st().result!;
    expect(r.max_profit_unbounded).toBe(true);
    expect(r.risk_reward_ratio).toBe(0);
  });
});

describe('an option leg still needs its volatility', () => {
  beforeEach(() => reset({ symbol: 'SPY', assetClass: 'EQUITY' }));

  it('is refused with the volatility wording, and the engine is not asked', async () => {
    st().addLeg(optionNoIv);
    await st().calculateStrategy();
    expect(st().notReady).toMatch(/implied volatility/i);
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
  });

  it('is still refused when a volatility-free future sits beside it', async () => {
    st().addLeg(future);
    st().addLeg(optionNoIv);
    await st().calculateStrategy();
    expect(st().notReady).toMatch(/implied volatility/i);
  });
});

describe('contract multiplier on the wire', () => {
  it('sends the leg multiplier when it has one', () => {
    const req = buildStrategyRequest({
      symbol: 'ES', spotPrice: 7790.9, impliedVolatility: 0, riskFreeRate: 0.0385, dividendYield: 0,
      horizonDays: 72, legs: [{ id: 'a', ...future }],
    });
    expect(req.getLegsList()[0].getContractMultiplier()).toBe(50);
  });

  it('keeps 100 for an option and 1 for a leg that says nothing', () => {
    const req = buildStrategyRequest({
      symbol: 'SPY', spotPrice: 779, impliedVolatility: 0.1, riskFreeRate: 0.0385, dividendYield: 0,
      horizonDays: 7,
      legs: [
        { id: 'o', ...optionNoIv },
        { id: 's', instrument_type: 'INSTRUMENT_EQUITY_SPOT', action: 'BUY', option_type: 'STOCK', strike_price: 779, premium: 779, quantity: 1 },
      ],
    });
    expect(req.getLegsList()[0].getContractMultiplier()).toBe(100);
    expect(req.getLegsList()[1].getContractMultiplier()).toBe(1);
  });
});

describe('futures contract specifications', () => {
  it('knows what a point is worth for the two roots the engine can quote', () => {
    expect(futuresMultiplier('ES')).toBe(50);
    expect(futuresMultiplier('NQ')).toBe(20);
  });

  it('does not guess a root it has no specification for', () => {
    expect(futuresMultiplier('ZZ')).toBeUndefined();
  });
});

