/**
 * Canary: proves the harness actually drives the real store.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Every other test file rests on this seam, so it is checked on its own first.
 * A harness that silently fails to intercept would let the suite pass while
 * testing nothing -- the exact class of defect this suite was created to catch,
 * reproduced one level up.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  settle,
} from '../test/grpc-harness';

const fake = createFakeClient({
  getMarketChain: () =>
    ({
      ok: chainResponse(
        [chainExpiration('2026-08-17', 7), chainExpiration('2026-08-24', 14)],
        [chainStrike(773, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.101 })],
        '2026-08-17'
      ),
    }),
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

describe('test harness', () => {
  beforeEach(() => {
    fake.calls.length = 0;
  });

  it('intercepts the chain RPC instead of reaching the network', async () => {
    useCalculatorStore.getState().loadChain('2026-08-17');
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    // If the fake were not installed, this call would have gone to the real
    // backend and `calls` would be empty.
    expect(fake.calls.map((c) => c.method)).toContain('getMarketChain');
  });

  it('lands the scripted response in real store state', async () => {
    useCalculatorStore.getState().loadChain('2026-08-17');
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    const st = useCalculatorStore.getState();
    expect(st.chainExpirations.map((e) => e.date)).toEqual(['2026-08-17', '2026-08-24']);
    expect(st.chainExpirations[0].dte).toBe(7);
    expect(st.chainStrikes).toHaveLength(1);
    expect(st.chainStrikes[0].strike).toBe(773);
  });
});
