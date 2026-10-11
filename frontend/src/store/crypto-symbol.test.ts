/**
 * A crypto symbol is not priced off a stock that shares its ticker (OFC-015).
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Observed on production: type BTC and the header read `BTC | CRYPTO | 37.84 |
 * QUOTE`. 37.84 is not a Bitcoin price; it is the price of a US-listed fund whose
 * ticker happens to be BTC -- the same collision this repository documents for
 * ES, the E-mini S&P root and Eversource Energy. A wrong price wearing a QUOTE
 * chip is worse than no price, and the chain beside it said (correctly) that no
 * option chain exists for the class.
 *
 * Nothing here can quote crypto and nothing lists options on it, so the store
 * asks for neither: no price, no chain request, and a message that says what to
 * do instead.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { createFakeClient, quoteResponse, chainResponse } from '../test/grpc-harness';

const fake = createFakeClient({
  getMarketQuote: () => ({ ok: quoteResponse(37.84) }),
  getMarketChain: () => ({ ok: chainResponse([], [], '') }),
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

const st = () => useCalculatorStore.getState();

describe('setSymbol for a crypto symbol', () => {
  beforeEach(() => {
    fake.calls.length = 0;
    useCalculatorStore.setState({ symbol: 'SPY', assetClass: 'EQUITY', legs: [], spotPrice: 779, retiredPosition: null });
  });

  it('classifies it as crypto', () => {
    st().setSymbol('BTC');
    expect(st().assetClass).toBe('CRYPTO');
  });

  it('shows no price, rather than a stock\'s price under a crypto ticker', () => {
    st().setSymbol('BTC');
    expect(st().spotPrice).toBe(0);
  });

  it('asks the quote service for nothing', () => {
    st().setSymbol('BTC');
    expect(fake.calls.some((c) => c.method === 'getMarketQuote')).toBe(false);
  });

  it('does not wait on a chain that cannot exist, and says what to do', () => {
    st().setSymbol('BTC');
    expect(fake.calls.some((c) => c.method === 'getMarketChain')).toBe(false);
    expect(st().chainStatus).toBe('error');
    expect(st().chainError).toMatch(/crypto/i);
    expect(st().chainError).toMatch(/SPY/);
  });

  it('still prices an ordinary equity as before', () => {
    st().setSymbol('NVDA');
    expect(fake.calls.some((c) => c.method === 'getMarketQuote')).toBe(true);
    expect(fake.calls.some((c) => c.method === 'getMarketChain')).toBe(true);
  });
});
