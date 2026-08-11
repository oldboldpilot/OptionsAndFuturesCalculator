/**
 * Coverage for `commitTicket` / `setTicket` in `useCalculatorStore.ts`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The suite exists because of one specific production defect: `commitTicket`
 * used to resolve the leg's expiration by reading `t.expiration` alone --
 *
 *   const dte = get().chainExpirations.find((e) => e.date === t.expiration)?.dte ?? 0;
 *
 * -- while `OptionTicket.tsx` DISPLAYS `ticket.expiration || selectedExpiration`.
 * `ticket.expiration` stays `''` until the user touches the Expiry dropdown, so
 * on the default path (load a chain, pick a strike, press Add, never touch
 * Expiry) the lookup missed and `?? 0` turned that miss into a fabricated
 * `expiration_days: 0`. The leg looked complete -- strike, premium, quantity,
 * IV were all present -- so nothing threw. Every downstream analytical panel
 * then refused with "No expiration on any leg" while the Expiry dropdown sat
 * there visibly showing a real date. That gap between what the UI *showed* and
 * what commitTicket *read* is exactly what test 1 below re-creates and pins.
 *
 * Fixed shape, read from useCalculatorStore.ts: `commitTicket` now resolves
 * `t.expiration || get().selectedExpiration`, looks THAT up, and refuses with
 * 'Pick an expiry before adding the leg.' when the lookup misses rather than
 * defaulting to 0.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  settle,
} from '../test/grpc-harness';

// Chain used by every test: two real expirations, one of them same-day (dte 0)
// to cover requirement 5 -- a legitimate zero must survive, not be confused
// with the fabricated zero the regression produced.
const fake = createFakeClient({
  getMarketChain: () =>
    ({
      ok: chainResponse(
        [
          chainExpiration('2026-08-17', 7),
          chainExpiration('2026-08-24', 14),
          chainExpiration('2026-08-10', 0, 'Today'),
        ],
        [chainStrike(580, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.22 })],
        '2026-08-17'
      ),
    }),
});

// Same vi.mock shape as harness.canary.test.ts -- vi.mock is hoisted, so each
// test file declares its own copy at top level rather than sharing a helper.
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

/** Reset the store's mutable pieces between tests so none of them leak state. */
function resetStore() {
  useCalculatorStore.setState({
    legs: [],
    error: null,
    chainStrikes: [],
    chainExpirations: [],
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
  });
}

/** Load the chain and wait for it to land, the precondition every test below shares. */
async function loadReadyChain() {
  useCalculatorStore.getState().loadChain('2026-08-17');
  await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');
}

describe('commitTicket', () => {
  beforeEach(() => {
    fake.calls.length = 0;
    resetStore();
  });

  // ---------------------------------------------------------------------
  // 1. THE REGRESSION ITSELF. This is the single most important test in the
  // suite. The production fix actually shipped in two halves that now both
  // sit in front of this bug: `loadChain` seeds `ticket.expiration` from the
  // resolved chain date, and `commitTicket` falls back to
  // `selectedExpiration` when `t.expiration` is empty. Together they mean
  // that on today's code, `ticket.expiration` is essentially never '' once a
  // chain has loaded successfully -- the seeding alone already closes the
  // gap on the ordinary click-through path.
  //
  // That is exactly why this test forces `ticket.expiration` back to ''
  // AFTER a successful chain load, via a direct `setState` rather than
  // `setTicket` -- to make unmistakable that nothing in the test chose an
  // expiry, and to reproduce the precise state commitTicket saw in
  // production: a loaded chain, a real `selectedExpiration`, and an empty
  // `t.expiration`. That isolates the fix this test exists to pin --
  // commitTicket's OWN fallback and lookup -- from the seeding fix, so
  // reverting either one independently is caught. Skipping this reset would
  // let the test pass for the wrong reason (loadChain's seed alone), which
  // is worse than not having the test at all: a green suite hiding a broken
  // guard is exactly the failure mode this whole file exists to prevent.
  //
  // Verified discriminating: run against the pre-fix line
  //   const dte = get().chainExpirations.find((e) => e.date === t.expiration)?.dte ?? 0;
  // (looks up `t.expiration` only, defaults via `?? 0`, no refusal) this
  // test FAILED -- expiration_days came back 0 instead of 7. Restoring the
  // fixed `t.expiration || get().selectedExpiration` resolution with a
  // refusal on miss made it pass again. See the task report for both raw
  // outcomes.
  // ---------------------------------------------------------------------
  it('resolves expiration_days from selectedExpiration when ticket.expiration was never touched', async () => {
    await loadReadyChain();

    // Simulate the exact production precondition: a chain is loaded and
    // selectedExpiration is real, but the ticket's own expiration field is
    // empty because the user never touched the Expiry dropdown. Using
    // setState (not setTicket) makes clear this models an unset field, not a
    // deliberate user choice of ''.
    useCalculatorStore.setState((s) => ({ ticket: { ...s.ticket, expiration: '' } }));

    // Never call setTicket({ expiration: ... }) -- this is the whole point.
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6 });
    expect(useCalculatorStore.getState().ticket.expiration).toBe('');
    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-08-17');

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.legs).toHaveLength(1);
    // The regression's signature: expiration_days landed as 0 no matter which
    // expiry was actually selected. Chain here selects 2026-08-17, dte 7.
    expect(st.legs[0].expiration_days).toBe(7);
    expect(st.legs[0].expiration_days).not.toBe(0);
  });

  // 1b. The companion case: prove `loadChain`'s seeding is ALSO load-bearing
  // by itself, on the literal default path with no manual intervention at
  // all. This is the scenario the original bug report describes end to end,
  // and it is what a real user actually does -- it just happens to no longer
  // discriminate the commitTicket line alone, because the seed already
  // supplies a non-empty `t.expiration` before commitTicket ever runs.
  it('the ordinary default path (chain load, pick strike, never touch Expiry) also lands the real dte', async () => {
    await loadReadyChain();

    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6 });
    // loadChain's seeding is what makes this true without any explicit
    // expiration set by the test -- see the comment on the test above.
    expect(useCalculatorStore.getState().ticket.expiration).toBe('2026-08-17');

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.legs).toHaveLength(1);
    expect(st.legs[0].expiration_days).toBe(7);
  });

  // 2. Explicitly picking a DIFFERENT expiry than the seeded default must use
  // that expiry's own dte, not the one selectedExpiration resolved to at load
  // time. Proves the lookup keys off the ticket's own expiration when it is
  // actually set, not unconditionally off selectedExpiration.
  it('uses the explicitly chosen expiry, not the chain-selected default', async () => {
    await loadReadyChain();
    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-08-17');

    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, expiration: '2026-08-24' });
    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.legs).toHaveLength(1);
    expect(st.legs[0].expiration_days).toBe(14);
  });

  // 5. A legitimate same-day expiry (dte 0) is a real horizon, not the
  // fabricated placeholder the bug produced. commitTicket must ADD the leg
  // with expiration_days: 0 and let the later calculate-time guard, not this
  // one, decide what to do with a same-day position.
  it('accepts a genuine same-day expiry (dte 0) and still adds the leg', async () => {
    await loadReadyChain();

    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, expiration: '2026-08-10' });
    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.legs).toHaveLength(1);
    expect(st.legs[0].expiration_days).toBe(0);
  });

  // --- Refusals: assert both that nothing was added AND the exact wording. ---

  it('refuses with no strike selected', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ premium: 4.6 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('Pick a strike before adding the leg.');
  });

  it('refuses a strike of 0', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 0, premium: 4.6 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('Pick a strike before adding the leg.');
  });

  it('refuses a negative strike', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: -10, premium: 4.6 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('Pick a strike before adding the leg.');
  });

  it('refuses with no premium quoted', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('This contract has no quoted price. Enter the price you would pay or receive.');
  });

  it('refuses a premium of 0', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 0 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('This contract has no quoted price. Enter the price you would pay or receive.');
  });

  it('refuses a negative premium', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: -1 });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('This contract has no quoted price. Enter the price you would pay or receive.');
  });

  it('refuses an expiration that matches nothing in chainExpirations', async () => {
    await loadReadyChain();
    // A date not present in the scripted chain at all -- the lookup-miss path,
    // distinct from the "never touched the field" path test 1 covers.
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, expiration: '2099-01-01' });

    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(0);
    expect(st.error).toBe('Pick an expiry before adding the leg.');
  });

  // --- Field mapping onto the committed Leg. ---

  it('maps action, optionType and instrument_type straight onto the leg', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({
      strike: 580,
      premium: 4.6,
      action: 'SELL',
      optionType: 'PUT',
    });

    useCalculatorStore.getState().commitTicket();

    const leg = useCalculatorStore.getState().legs[0];
    expect(leg.action).toBe('SELL');
    expect(leg.option_type).toBe('PUT');
    expect(leg.instrument_type).toBe('INSTRUMENT_EQUITY_OPTION');
    expect(leg.strike_price).toBe(580);
    expect(leg.premium).toBe(4.6);
  });

  it('carries a positive quantity through unchanged', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, quantity: 5 });

    useCalculatorStore.getState().commitTicket();

    expect(useCalculatorStore.getState().legs[0].quantity).toBe(5);
  });

  it('coerces a non-positive quantity to 1 rather than committing a zero or negative size', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, quantity: 0 });
    useCalculatorStore.getState().commitTicket();
    expect(useCalculatorStore.getState().legs[0].quantity).toBe(1);

    resetStore();
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, quantity: -3 });
    useCalculatorStore.getState().commitTicket();
    expect(useCalculatorStore.getState().legs[0].quantity).toBe(1);
  });

  it('carries implied_volatility through when the ticket has one', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, impliedVolatility: 0.35 });

    useCalculatorStore.getState().commitTicket();

    expect(useCalculatorStore.getState().legs[0].implied_volatility).toBe(0.35);
  });

  it('leaves implied_volatility undefined when the ticket never got one', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6 });

    useCalculatorStore.getState().commitTicket();

    expect(useCalculatorStore.getState().legs[0].implied_volatility).toBeUndefined();
  });
});
