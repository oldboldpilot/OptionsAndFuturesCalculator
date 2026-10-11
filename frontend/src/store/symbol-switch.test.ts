/**
 * Switching symbol must not carry a position across (OFC-003).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Observed on production: with BUY CALL 779 on SPY the Outcome read "Max profit
 * $19,037 / Max loss -$449". Clicking NVDA changed the header to 239.86 and
 * blanked the Outcome, but the Position still read `BUY CALL 779.00`, and
 * Recalculate answered "Max loss -$449, probability of profit 0.0%, delta
 * 0.000" -- SPY's strike and SPY's implied volatility priced against NVDA's
 * spot. A confident, plausible, meaningless answer in a financial tool.
 *
 * The decision recorded here is to CLEAR the position on a change of
 * instrument and keep it for one Undo, rather than to keep it and refuse to
 * price it. A position has one underlying: the engine prices every leg off one
 * spot, so a SPY leg and an NVDA leg can never be in the same position, and
 * keeping the old legs on screen would also have to stop the user adding new
 * ones. Refusing to calculate is still done, as defence in depth, for the ways
 * a foreign leg can arrive that are not a symbol click (a loaded scenario, a
 * future code path) -- but it is the second line, not the design.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  rateResponse,
  quoteResponse,
  strategyResponse,
  settle,
} from '../test/grpc-harness';

const PRICES: Record<string, number> = { SPY: 779.09, NVDA: 239.86, QQQ: 520.1, ES: 7790.9 };

const fake = createFakeClient({
  getMarketQuote: (req) => ({
    ok: quoteResponse(PRICES[(req as { getSymbol(): string }).getSymbol()] ?? 100),
  }),
  getMarketChain: () => ({
    ok: chainResponse(
      [chainExpiration('2026-10-13', 7)],
      [chainStrike(780, { isAtm: true, callBid: 4.4, callAsk: 4.49, callIv: 0.105 })],
      '2026-10-13',
    ),
  }),
  getRiskFreeRate: () => ({ ok: rateResponse(0.0385) }),
  calculateStrategy: () => ({ ok: strategyResponse({ maxProfit: 19037.25, maxLoss: -449 }) }),
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

const spyCall = {
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 779,
  premium: 4.49,
  quantity: 1,
  expiration_days: 7,
  implied_volatility: 0.105,
};

function reset() {
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    assetClass: 'EQUITY',
    legs: [],
    spotPrice: 779.09,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    notReady: null,
    gateDenied: null,
    modelLimit: null,
    isLoading: false,
    retiredPosition: null,
  });
}

const st = () => useCalculatorStore.getState();

describe('legs carry the instrument they were built on', () => {
  beforeEach(reset);

  it('stamps the current symbol and asset class on a new leg', () => {
    st().addLeg(spyCall);
    expect(st().legs[0].symbol).toBe('SPY');
    expect(st().legs[0].asset_class).toBe('EQUITY');
  });

  it('keeps an explicit symbol rather than overwriting it', () => {
    st().addLeg({ ...spyCall, symbol: 'QQQ', asset_class: 'EQUITY' });
    expect(st().legs[0].symbol).toBe('QQQ');
  });
});

describe('setSymbol with a position on screen', () => {
  beforeEach(reset);

  it('clears the legs and the stale result when the instrument changes', () => {
    st().addLeg(spyCall);
    useCalculatorStore.setState({ result: { max_profit: 1 } as never });

    st().setSymbol('NVDA');

    expect(st().legs).toEqual([]);
    expect(st().result).toBeNull();
    expect(st().symbol).toBe('NVDA');
  });

  it('keeps the old position for one undo, naming where it came from', () => {
    st().addLeg(spyCall);
    st().setSymbol('NVDA');

    const kept = st().retiredPosition;
    expect(kept).not.toBeNull();
    expect(kept!.symbol).toBe('SPY');
    expect(kept!.assetClass).toBe('EQUITY');
    expect(kept!.legs).toHaveLength(1);
    expect(kept!.legs[0].strike_price).toBe(779);
  });

  it('leaves the position alone when the same instrument is selected again', () => {
    st().addLeg(spyCall);
    st().setSymbol('SPY');
    expect(st().legs).toHaveLength(1);
    expect(st().retiredPosition).toBeNull();
  });

  it('treats the same ticker as a different instrument when the asset class differs', () => {
    // ES is both the E-mini S&P root and Eversource Energy's NYSE ticker.
    st().addLeg({ ...spyCall });
    st().setSymbol('SPY', undefined, 'FUTURES');
    expect(st().legs).toEqual([]);
    expect(st().retiredPosition?.assetClass).toBe('EQUITY');
  });

  it('does not offer an undo when there was nothing to lose', () => {
    st().setSymbol('NVDA');
    expect(st().retiredPosition).toBeNull();
  });

  it('forgets the retired position once the user builds a new one', () => {
    st().addLeg(spyCall);
    st().setSymbol('NVDA');
    st().addLeg({ ...spyCall, strike_price: 240 });
    expect(st().retiredPosition).toBeNull();
  });

  it('sends nothing to the engine for the cleared position', async () => {
    st().addLeg(spyCall);
    st().setSymbol('NVDA');
    await st().calculateStrategy();
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
  });
});

describe('undoSymbolSwitch', () => {
  beforeEach(reset);

  it('restores the symbol, asset class and legs together', () => {
    st().addLeg(spyCall);
    st().setSymbol('NVDA');
    st().undoSymbolSwitch();

    expect(st().symbol).toBe('SPY');
    expect(st().assetClass).toBe('EQUITY');
    expect(st().legs).toHaveLength(1);
    expect(st().legs[0].symbol).toBe('SPY');
    expect(st().retiredPosition).toBeNull();
  });

  it('prices the restored position against SPY once its quote arrives, not against NVDA', async () => {
    st().addLeg(spyCall);
    st().setSymbol('NVDA');
    await settle(() => st().spotPrice === PRICES.NVDA, 'NVDA quote');
    fake.calls.length = 0;

    st().undoSymbolSwitch();
    await settle(
      () => fake.calls.some((c) => c.method === 'calculateStrategy'),
      'a calculation after the SPY quote lands',
    );

    const sent = fake.calls.find((c) => c.method === 'calculateStrategy')!.request as {
      getUnderlyingSymbol(): string;
      getCurrentPrice(): number;
    };
    expect(sent.getUnderlyingSymbol()).toBe('SPY');
    expect(sent.getCurrentPrice()).toBe(PRICES.SPY);
  });

  it('is a no-op when there is nothing to restore', () => {
    st().undoSymbolSwitch();
    expect(st().symbol).toBe('SPY');
    expect(st().legs).toEqual([]);
  });
});

describe('calculateStrategy refuses a position that is not on the current instrument', () => {
  beforeEach(reset);

  it('names both symbols and sends nothing to the engine', async () => {
    // Reached any way other than a symbol click -- here by writing the state
    // directly, as a loaded scenario or a future code path could.
    useCalculatorStore.setState({
      symbol: 'NVDA',
      spotPrice: 239.86,
      legs: [{ id: 'leg-x', ...spyCall, symbol: 'SPY', asset_class: 'EQUITY' }],
    });

    await st().calculateStrategy();

    expect(st().notReady).toMatch(/SPY/);
    expect(st().notReady).toMatch(/NVDA/);
    expect(st().result).toBeNull();
    expect(st().error).toBeNull();
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(false);
  });

  it('still prices a leg that predates the stamp (no symbol recorded)', async () => {
    useCalculatorStore.setState({
      legs: [{ id: 'leg-old', ...spyCall }],
    });
    await st().calculateStrategy();
    expect(fake.calls.some((c) => c.method === 'calculateStrategy')).toBe(true);
  });
});
