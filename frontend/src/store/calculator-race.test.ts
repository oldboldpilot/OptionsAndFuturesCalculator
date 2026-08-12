/**
 * Overlapping `calculateStrategy` calls must not let an older answer win.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This overlap is the ordinary case, not an edge one. `PositionLegs` calls
 * `calculateStrategy()` from its own change handler after `updateLeg`, and
 * `StrategyWorkspace` fires it again from a `useEffect` on `legs` -- so every
 * quantity keystroke starts two requests, and a three-digit quantity starts
 * six. Nothing orders the responses.
 *
 * Four distinct ways the older response can win, one test each:
 *
 *   1. it overwrites a newer `result`, so the panel shows numbers for a
 *      position the store no longer holds;
 *   2. its PERMISSION_DENIED blanks a newer live result and raises an upgrade
 *      prompt over it -- `StrategyMetrics` checks `gateDenied` BEFORE it
 *      renders `result`, so the valid answer is not merely accompanied by the
 *      refusal, it is replaced by it;
 *   3. its FAILED_PRECONDITION does the same through `modelLimit`;
 *   4. it clears `isLoading` while the newer request is still in flight, so
 *      the spinner stops on a calculation that has not answered.
 *
 * The fix is the staleness token `useTreePricerStore` already uses: capture a
 * sequence number at entry and commit nothing if it has moved on. These tests
 * pin the OUTCOME rather than the token, so the mechanism can change.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  quoteResponse,
  chainExpiration,
  chainStrike,
  chainResponse,
  rateResponse,
  strategyResponse,
  settle,
  FakeStrategyResponse,
  GRPC_PERMISSION_DENIED,
  GRPC_FAILED_PRECONDITION,
  GRPC_UNAVAILABLE,
} from '../test/grpc-harness';

type Outcome = { ok: FakeStrategyResponse } | { fail: { code: number; message: string } };

/**
 * One in-flight `calculateStrategy`, settled by hand.
 *
 * A test drives resolution ORDER by settling these out of the order they were
 * created, which is the only way to reproduce "the older request answers
 * second" -- the entire subject of this file.
 */
interface Inflight {
  promise: Promise<Outcome>;
  settle: (o: Outcome) => void;
}

let inflight: Inflight[] = [];
/** Makes the Treasury fetch fail, so `riskFreeRate` stays null after the await. */
let rateFails = false;
/** Fired INSIDE the rate RPC, which is the seam for acting while it is parked. */
let rateSideEffect: (() => void) | null = null;

function nextInflight(): Inflight {
  let settleFn!: (o: Outcome) => void;
  const promise = new Promise<Outcome>((resolve) => {
    settleFn = resolve;
  });
  return { promise, settle: settleFn };
}

const fake = createFakeClient({
  getMarketQuote: () => ({ ok: quoteResponse(580) }),
  getMarketChain: () => ({
    ok: chainResponse(
      [chainExpiration('2026-08-17', 7)],
      [chainStrike(580, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.2 })],
      '2026-08-17'
    ),
  }),
  getRiskFreeRate: () => {
    if (rateSideEffect) rateSideEffect();
    return rateFails
      ? { fail: { code: GRPC_UNAVAILABLE, message: 'Treasury feed is unavailable' } }
      : { ok: rateResponse(0.0385) };
  },
  calculateStrategy: () => {
    const pending = nextInflight();
    inflight.push(pending);
    return pending.promise;
  },
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
  id: 'leg-race-1',
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 580,
  premium: 4.5,
  quantity: 1,
  expiration_days: 7,
  implied_volatility: 0.2,
};

/**
 * Start a calculation and wait until its request has actually reached the
 * fake.
 *
 * Awaiting the fake rather than a fixed number of ticks matters: the action
 * has a conditional `await` on the Treasury rate before it sends anything, so
 * "the call has been made" and "the promise has been created" are not the same
 * moment, and a test that assumed they were would sometimes settle a request
 * that did not exist yet.
 *
 * The handle comes back BOXED, and it has to. `start` is async, and `return p`
 * in an async function is `return await p` -- the outer promise adopts the
 * inner one. Returning the running calculation bare would make `await start()`
 * wait for a request this test has not settled yet, which deadlocks every case
 * in this file at its first line.
 */
async function start(): Promise<{ done: Promise<void> }> {
  const expected = inflight.length + 1;
  const done = useCalculatorStore.getState().calculateStrategy();
  await settle(() => inflight.length === expected, `request ${expected} in flight`);
  return { done };
}

function resetStore() {
  inflight = [];
  // Reset HERE rather than at the end of the test that sets them: a settle()
  // timeout throws before any cleanup line would run, and a live rateSideEffect
  // empties `legs` for every test after it.
  rateFails = false;
  rateSideEffect = null;
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    assetClass: 'EQUITY',
    legs: [leg as never],
    spotPrice: 580,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    dividendYield: 0,
    rateMeta: null,
    result: null,
    isLoading: false,
    error: null,
    notReady: null,
    gateDenied: null,
    modelLimit: null,
  });
}

describe('overlapping calculateStrategy calls', () => {
  beforeEach(() => {
    resetStore();
  });

  it('a stale success does not overwrite the newer result', async () => {
    const first = (await start()).done;
    const second = (await start()).done;

    // The NEWER request answers first, and its answer is the one the user is
    // entitled to see: it is the one that describes the position on screen.
    inflight[1].settle({ ok: strategyResponse({ maxProfit: 2222 }) });
    await second;
    expect(useCalculatorStore.getState().result?.max_profit).toBe(2222);

    // The older request answers second, carrying the older position's numbers.
    inflight[0].settle({ ok: strategyResponse({ maxProfit: 1111 }) });
    await first;

    expect(useCalculatorStore.getState().result?.max_profit).toBe(2222);
  });

  it('a stale PERMISSION_DENIED does not blank a newer live result', async () => {
    const first = (await start()).done;
    const second = (await start()).done;

    inflight[1].settle({ ok: strategyResponse({ maxProfit: 2222 }) });
    await second;

    inflight[0].settle({ fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro' } });
    await first;

    const st = useCalculatorStore.getState();
    expect(st.result?.max_profit).toBe(2222);
    expect(st.gateDenied).toBeNull();
  });

  it('a stale FAILED_PRECONDITION does not blank a newer live result', async () => {
    const first = (await start()).done;
    const second = (await start()).done;

    inflight[1].settle({ ok: strategyResponse({ maxProfit: 2222 }) });
    await second;

    inflight[0].settle({
      fail: { code: GRPC_FAILED_PRECONDITION, message: 'Asian legs are not priced by this model.' },
    });
    await first;

    const st = useCalculatorStore.getState();
    expect(st.result?.max_profit).toBe(2222);
    expect(st.modelLimit).toBeNull();
  });

  it('a stale generic error does not blank a newer live result', async () => {
    const first = (await start()).done;
    const second = (await start()).done;

    inflight[1].settle({ ok: strategyResponse({ maxProfit: 2222 }) });
    await second;

    inflight[0].settle({ fail: { code: 13, message: 'engine exploded' } });
    await first;

    const st = useCalculatorStore.getState();
    expect(st.result?.max_profit).toBe(2222);
    expect(st.error).toBeNull();
  });

  it('a superseded call leaves isLoading set while the newer one is in flight', async () => {
    const first = (await start()).done;
    const second = (await start()).done;

    // The OLDER request answers first this time. It is already superseded, so
    // it must commit nothing at all -- including `isLoading: false`, which
    // would stop the spinner on a calculation that has not answered.
    inflight[0].settle({ ok: strategyResponse({ maxProfit: 1111 }) });
    await first;

    const mid = useCalculatorStore.getState();
    expect(mid.isLoading).toBe(true);
    expect(mid.result).toBeNull();

    inflight[1].settle({ ok: strategyResponse({ maxProfit: 2222 }) });
    await second;

    const end = useCalculatorStore.getState();
    expect(end.isLoading).toBe(false);
    expect(end.result?.max_profit).toBe(2222);
  });

  it('the newest call still commits normally when nothing supersedes it', async () => {
    // The guard must not be so eager that it swallows the ordinary path. A
    // token check written against the wrong sequence number would make every
    // calculation a no-op, and every test above would still pass.
    const only = (await start()).done;
    inflight[0].settle({ ok: strategyResponse({ maxProfit: 1275, maxLoss: -725, breakEven: 587.25 }) });
    await only;

    const st = useCalculatorStore.getState();
    expect(st.isLoading).toBe(false);
    expect(st.result?.max_profit).toBe(1275);
    expect(st.result?.break_even).toBe(587.25);
    expect(st.error).toBeNull();
    expect(st.gateDenied).toBeNull();
  });

  it('a precondition refusal that overtakes an in-flight call stops the spinner', async () => {
    const first = (await start()).done;
    expect(useCalculatorStore.getState().isLoading).toBe(true);

    // The user empties the position while the request is in flight. This call
    // supersedes it and refuses before sending anything, so the overtaken
    // request will now commit nothing -- and it used to be the only writer of
    // `isLoading: false`. If the precondition return does not clear the flag,
    // nothing ever will.
    useCalculatorStore.setState({ legs: [] });
    await useCalculatorStore.getState().calculateStrategy();

    expect(useCalculatorStore.getState().isLoading).toBe(false);

    // And the overtaken request must still not land a result for the position
    // the user just emptied.
    inflight[0].settle({ ok: strategyResponse({ maxProfit: 1111 }) });
    await first;

    const st = useCalculatorStore.getState();
    expect(st.result).toBeNull();
    expect(st.isLoading).toBe(false);
  });

  it('a superseded call does not report a Treasury failure over the newer state', async () => {
    // The overtaking happens while this call is parked in the RATE fetch,
    // which is the first of its two awaits -- a window the staleness check
    // after the RPC cannot cover.
    rateFails = true;
    useCalculatorStore.setState({ riskFreeRate: null, rateSource: 'pending' });
    rateSideEffect = () => {
      // Runs inside the rate RPC. Empties the position and supersedes the
      // parked call, which will resume to find no rate and must stay quiet.
      rateSideEffect = null;
      useCalculatorStore.setState({ legs: [] });
      void useCalculatorStore.getState().calculateStrategy();
    };

    await useCalculatorStore.getState().calculateStrategy();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.notReady).toBeNull();
    expect(st.result).toBeNull();
    expect(st.isLoading).toBe(false);
  });

  it('the newest call still reports its own PERMISSION_DENIED', async () => {
    const only = (await start()).done;
    inflight[0].settle({ fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro' } });
    await only;

    const st = useCalculatorStore.getState();
    expect(st.gateDenied).toBe('Needs Pro');
    expect(st.result).toBeNull();
    expect(st.isLoading).toBe(false);
  });
});
