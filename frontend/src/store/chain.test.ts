/**
 * `loadChain`, `setSelectedExpiration` and `setSymbol` in `useCalculatorStore`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Two production bugs anchor this file. First: `OptionTicket` displays
 * `ticket.expiration || selectedExpiration` while `commitTicket` used to read
 * only `ticket.expiration`, so a user who never touched the Expiry dropdown --
 * the default path -- committed a leg whose expiration was `''` while the
 * dropdown visibly showed a real date. The fix seeds `ticket.expiration` from
 * the same resolved value in the same `set()` call that seeds
 * `selectedExpiration`; the lockstep invariant is what this file pins.
 *
 * Second: a futures symbol's chain has a forward curve and NO option strikes,
 * and an earlier version of this store's strikes-only check called that
 * `error` and blanked the whole panel. This file locks the futures case to
 * `ready`.
 *
 * The harness (`../test/grpc-harness.ts`) has no futures-contract builder --
 * every other suite reads options chains only -- so this file adds a small
 * local one. Reported as a gap in the final task summary rather than silently
 * patching the shared harness, which the task forbade editing.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  settle,
  GRPC_UNAVAILABLE,
  type FakeChainResponse,
  type FakeChainStrike,
  type RpcOutcome,
} from '../test/grpc-harness';

/**
 * A futures contract on the forward curve, built locally.
 *
 * The harness's `chainResponse(exps, strikes, selected, futures)` takes
 * `futures` as `unknown[]` precisely so a caller can hand it objects like
 * this one -- the store only ever calls the getters below
 * (`useCalculatorStore.ts`'s `futuresCurve` mapping), so that is all this
 * needs to carry. Every field gets a distinct value for the same reason the
 * strike builder below does: a transposed field (e.g. `basis` and
 * `annualizedYield` swapped) would otherwise be invisible.
 */
function futuresContract(
  over: Partial<{
    code: string;
    deliveryMonth: string;
    daysToExpiry: number;
    futuresPrice: number;
    basis: number;
    annualizedYield: number;
    state: string;
  }> = {}
) {
  const {
    code = 'ESZ26',
    deliveryMonth = '2026-12',
    daysToExpiry = 45,
    futuresPrice = 5820.5,
    basis = 12.25,
    annualizedYield = 0.041,
    state = 'MODELLED',
  } = over;
  return {
    getCode: () => code,
    getDeliveryMonth: () => deliveryMonth,
    getDaysToExpiry: () => daysToExpiry,
    getFuturesPrice: () => futuresPrice,
    getBasis: () => basis,
    getAnnualizedYield: () => annualizedYield,
    getState: () => state,
  };
}

/**
 * A chain strike with every call/put field distinct.
 *
 * The harness's own `chainStrike()` hardcodes the put side (bid 1, ask 1.2,
 * delta -0.5, iv = callIv, volume 100, OI 500), which cannot catch a
 * transposed field -- a store that swapped `getPutBid`/`getPutAsk` would
 * still pass against those fixtures. This builder gives call and put disjoint
 * values on every field so a mapping mistake in either direction fails.
 */
function distinctStrike(strike: number): FakeChainStrike {
  return {
    getStrike: () => strike,
    getIsAtm: () => true,
    getCallBid: () => 4.1,
    getCallAsk: () => 4.3,
    getCallDelta: () => 0.62,
    getCallIv: () => 0.18,
    getCallVolume: () => 321,
    getCallOpenInterest: () => 654,
    getPutBid: () => 5.2,
    getPutAsk: () => 5.4,
    getPutDelta: () => -0.38,
    getPutIv: () => 0.24,
    getPutVolume: () => 987,
    getPutOpenInterest: () => 1200,
  };
}

/**
 * Scripted per test via `chainHandler`, the same pattern
 * `calculate-guards.test.ts` uses for `rateFails`: a mutable binding read at
 * call time so each test can pick the chain response without rebuilding the
 * client.
 */
let chainHandler: (req: unknown) => RpcOutcome<FakeChainResponse> = () => ({
  ok: chainResponse(
    [chainExpiration('2026-08-17', 7)],
    [chainStrike(773, { isAtm: true })],
    '2026-08-17'
  ),
});

const fake = createFakeClient({
  getMarketChain: (req) => chainHandler(req),
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

/** Reset to a known-clean chain/ticket state between tests. */
function reset(over: Record<string, unknown> = {}) {
  fake.calls.length = 0;
  chainHandler = () => ({
    ok: chainResponse(
      [chainExpiration('2026-08-17', 7)],
      [chainStrike(773, { isAtm: true })],
      '2026-08-17'
    ),
  });
  useCalculatorStore.setState({
    symbol: 'SPY',
    assetClass: 'EQUITY',
    chainStrikes: [],
    chainExpirations: [],
    futuresCurve: [],
    selectedExpiration: '',
    chainStatus: 'idle',
    chainError: null,
    ticket: {
      action: 'BUY',
      optionType: 'CALL',
      expiration: '',
      strike: null,
      premium: null,
      quantity: 1,
      impliedVolatility: null,
      asianType: 'NOT_ASIAN',
    },
    ...over,
  });
}

describe('loadChain: ticket/selection lockstep', () => {
  beforeEach(() => reset());

  it('seeds ticket.expiration to the same value as selectedExpiration', async () => {
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    const st = useCalculatorStore.getState();
    expect(st.ticket.expiration).toBe(st.selectedExpiration);
    expect(st.ticket.expiration).toBe('2026-08-17');
  });
});

describe('loadChain: expiration resolution precedence', () => {
  beforeEach(() => reset());

  it('an explicit loadChain(date) argument wins over the response', async () => {
    // The response answers with a DIFFERENT selected expiration; the argument
    // must still win, because it is what the caller (setSelectedExpiration)
    // just asked for.
    chainHandler = () => ({
      ok: chainResponse(
        [chainExpiration('2026-08-17', 7), chainExpiration('2026-08-24', 14)],
        [chainStrike(773)],
        '2026-08-17'
      ),
    });
    useCalculatorStore.getState().loadChain('2026-08-24');
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    const st = useCalculatorStore.getState();
    expect(st.selectedExpiration).toBe('2026-08-24');
    expect(st.ticket.expiration).toBe('2026-08-24');
  });

  it('falls back to the response selected-expiration when no argument and no prior selection', async () => {
    // No argument passed, and the store's own selectedExpiration starts ''
    // (reset() above), so `wanted` is falsy and the response's own answer
    // must be used instead.
    chainHandler = () => ({
      ok: chainResponse([chainExpiration('2026-08-17', 7)], [chainStrike(773)], '2026-08-17'),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-08-17');
  });

  it('resolves to empty string when neither an argument nor the response supplies one', async () => {
    chainHandler = () => ({
      ok: chainResponse([chainExpiration('2026-08-17', 7)], [chainStrike(773)], ''),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    const st = useCalculatorStore.getState();
    expect(st.selectedExpiration).toBe('');
    expect(st.ticket.expiration).toBe('');
  });
});

describe('loadChain: futures symbols', () => {
  beforeEach(() => reset({ symbol: 'ES', assetClass: 'FUTURES' }));

  it('is ready, not error, when the response carries a forward curve and no strikes', async () => {
    chainHandler = () => ({
      ok: chainResponse(
        [chainExpiration('2026-09-19', 54)],
        [],
        '2026-09-19',
        [futuresContract({ code: 'ESU26' }), futuresContract({ code: 'ESZ26', deliveryMonth: '2026-12' })]
      ),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus !== 'loading', 'chain settled');

    const st = useCalculatorStore.getState();
    // The regression this pins: a strikes-only check previously called this
    // combination `error` and cleared the panel even though the futures curve
    // had loaded correctly.
    expect(st.chainStatus).toBe('ready');
    expect(st.chainError).toBeNull();
    expect(st.chainStrikes).toHaveLength(0);
    expect(st.futuresCurve).toHaveLength(2);
    expect(st.futuresCurve[0].code).toBe('ESU26');
  });
});

describe('loadChain: no listed contracts', () => {
  beforeEach(() => reset());

  it('is error when the response carries neither strikes nor a futures curve', async () => {
    chainHandler = () => ({
      ok: chainResponse([chainExpiration('2026-08-17', 7)], [], '2026-08-17', []),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus !== 'loading', 'chain settled');

    const st = useCalculatorStore.getState();
    expect(st.chainStatus).toBe('error');
    expect(st.chainError).toContain('No listed contracts returned for SPY');
  });
});

describe('loadChain: RPC failure', () => {
  beforeEach(() => reset());

  it('sets error status and clears any strikes left from a previous successful load', async () => {
    // Load successfully first, so there is something stale to leave behind if
    // the failure path forgets to clear it.
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'first load ready');
    expect(useCalculatorStore.getState().chainStrikes.length).toBeGreaterThan(0);

    chainHandler = () => ({ fail: { code: GRPC_UNAVAILABLE, message: 'chain service down' } });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'error', 'second load error');

    const st = useCalculatorStore.getState();
    expect(st.chainError).toBe('chain service down');
    expect(st.chainStrikes).toEqual([]);
    expect(st.chainExpirations).toEqual([]);
    expect(st.futuresCurve).toEqual([]);
  });
});

describe('setSelectedExpiration', () => {
  beforeEach(() => reset());

  it('sets the selection and reloads the chain for that date', async () => {
    chainHandler = () => ({
      ok: chainResponse(
        [chainExpiration('2026-08-17', 7), chainExpiration('2026-08-24', 14)],
        [chainStrike(773)],
        '2026-08-24'
      ),
    });
    useCalculatorStore.getState().setSelectedExpiration('2026-08-24');
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-08-24');

    const chainCalls = fake.calls.filter((c) => c.method === 'getMarketChain');
    expect(chainCalls).toHaveLength(1);
    const req = chainCalls[0].request as { getExpirationDate(): string };
    expect(req.getExpirationDate()).toBe('2026-08-24');
  });
});

describe('loadChain: strike field mapping', () => {
  beforeEach(() => reset());

  it('lands bid/ask/delta/iv/volume/openInterest and isAtm on the correct call/put fields', async () => {
    chainHandler = () => ({
      ok: chainResponse([chainExpiration('2026-08-17', 7)], [distinctStrike(650)], '2026-08-17'),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    const [row] = useCalculatorStore.getState().chainStrikes;
    expect(row.strike).toBe(650);
    expect(row.isAtm).toBe(true);

    // A transposed put/call field is invisible to a user and wrong everywhere
    // downstream, so every field gets its own distinct value and its own
    // assertion rather than a single deep-equal that could pass on a
    // coincidental match.
    expect(row.call.bid).toBe(4.1);
    expect(row.call.ask).toBe(4.3);
    expect(row.call.delta).toBe(0.62);
    expect(row.call.iv).toBe(0.18);
    expect(row.call.volume).toBe(321);
    expect(row.call.openInterest).toBe(654);

    expect(row.put.bid).toBe(5.2);
    expect(row.put.ask).toBe(5.4);
    expect(row.put.delta).toBe(-0.38);
    expect(row.put.iv).toBe(0.24);
    expect(row.put.volume).toBe(987);
    expect(row.put.openInterest).toBe(1200);
  });
});

describe('loadChain: fetched_at propagation', () => {
  beforeEach(() => reset());

  it('populates chainFetchedAt from the response fetched_at timestamp', async () => {
    chainHandler = () => ({
      ok: chainResponse(
        [chainExpiration('2026-08-17', 7)],
        [distinctStrike(650)],
        '2026-08-17',
        [],
        '2026-08-11T14:15:00Z'
      ),
    });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');

    expect(useCalculatorStore.getState().chainFetchedAt).toBe('2026-08-11T14:15:00Z');
  });

  it('clears chainFetchedAt to null when load fails', async () => {
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');
    expect(useCalculatorStore.getState().chainFetchedAt).not.toBeNull();

    chainHandler = () => ({ fail: { code: GRPC_UNAVAILABLE, message: 'chain service down' } });
    useCalculatorStore.getState().loadChain();
    await settle(() => useCalculatorStore.getState().chainStatus === 'error', 'second load error');

    expect(useCalculatorStore.getState().chainFetchedAt).toBeNull();
  });
});
