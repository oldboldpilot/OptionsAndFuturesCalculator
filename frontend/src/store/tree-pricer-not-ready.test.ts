/**
 * A precondition is not a failure, and the tree pricer must not say it is.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `ExerciseStylePanel` renders three mutually exclusive states, and which one
 * a message lands in is the whole point: `gateDenied` is the upgrade prompt,
 * `error` is "Unavailable" in loss red, and the neutral branch is an invitation
 * to act. Every precondition in `executePricing` -- no spot, no strike, no IV,
 * no expiry, no Bermudan dates -- used to be written into `error`, so a trader
 * who had simply not picked a strike yet was told the calculator was
 * unavailable. Observed on production 2026-08-12: "Unavailable / Pick a strike
 * on the ticket before pricing exercise styles."
 *
 * That is the same defect this project already fixed once, when a
 * PERMISSION_DENIED entitlement refusal rendered under "Unavailable" instead of
 * the upgrade prompt -- see entitlement.test.ts. Fixing it in one place and
 * leaving the other is how it comes back.
 *
 * These tests pin the ROUTING, not the wording. A message may be reworded; a
 * precondition arriving in `error` is a regression.
 *
 * The calculator store had the SAME defect in `commitTicket` and in
 * `calculateStrategy`'s guards, rendering preconditions red across five
 * analytics panels. That is fixed too, by the same split -- see
 * `calculator-not-ready.test.ts`, which pins the routing on that side.
 */
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

/**
 * No fake client, deliberately. Every case here returns at a PRECONDITION,
 * before `executePricing` reaches any RPC -- and the tree pricer talks to
 * `FinanceClient` (dynamically imported inside the pricing action), not to
 * `OptionsCalculatorClient`, so scripting the latter would prove nothing. The
 * calculator module is still mocked because it constructs its client at import
 * time; the methods are never called.
 */
vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    getMarketQuote = vi.fn();
    getRiskFreeRate = vi.fn();
    getMarketChain = vi.fn();
    calculateStrategy = vi.fn();
  },
}));

const { useCalculatorStore } = await import('./useCalculatorStore');
const { useTreePricerStore } = await import('./useTreePricerStore');

/**
 * `priceTree` is debounced by 300 ms, and the harness's `settle` polls for far
 * less than that -- so a test that calls it and reads the store immediately
 * observes the state BEFORE anything ran, and passes or fails for reasons that
 * have nothing to do with routing. Fake timers make the wait explicit and
 * instant; `advanceTimersByTimeAsync` also flushes the microtasks so the async
 * `executePricing` reaches its early return.
 */
async function priceAndDrain() {
  useTreePricerStore.getState().priceTree();
  await vi.advanceTimersByTimeAsync(400);
}

/** A ticket with everything the pricer needs EXCEPT the one field under test. */
function readyTicket(overrides: Record<string, unknown> = {}) {
  useCalculatorStore.setState({
    symbol: 'SPY',
    spotPrice: 580,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    chainExpirations: [{ date: '2026-09-18', dte: 30, label: '2026-09-18' }],
    selectedExpiration: '2026-09-18',
    ticket: {
      action: 'BUY',
      optionType: 'CALL',
      expiration: '2026-09-18',
      strike: 580,
      premium: 7.5,
      impliedVolatility: 0.2,
      quantity: 1,
      ...overrides,
    } as never,
  });
  useTreePricerStore.setState({ error: null, notReady: null, gateDenied: null, results: [] });
}

describe('tree pricer preconditions are not errors', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    readyTicket();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('routes a missing strike to notReady, never to error', async () => {
    readyTicket({ strike: null });
    await priceAndDrain();

    const st = useTreePricerStore.getState();
    expect(st.notReady).toBeTruthy();
    expect(st.error).toBeNull();
    expect(st.gateDenied).toBeNull();
  });

  it('routes a missing expiry to notReady, never to error', async () => {
    // dte resolves from chainExpirations; an expiry the chain does not know
    // yields dte 0, which is the "no expiry on the ticket" branch.
    readyTicket({ expiration: '' });
    useCalculatorStore.setState({ selectedExpiration: '' });
    await priceAndDrain();

    const st = useTreePricerStore.getState();
    expect(st.notReady).toBeTruthy();
    expect(st.error).toBeNull();
  });

  it('routes a missing implied volatility to notReady, never to error', async () => {
    readyTicket({ impliedVolatility: null });
    await priceAndDrain();

    const st = useTreePricerStore.getState();
    expect(st.notReady).toBeTruthy();
    expect(st.error).toBeNull();
  });

  it('routes a missing spot price to notReady, never to error', async () => {
    readyTicket();
    useCalculatorStore.setState({ spotPrice: 0 });
    await priceAndDrain();

    const st = useTreePricerStore.getState();
    expect(st.notReady).toBeTruthy();
    expect(st.error).toBeNull();
  });

  it('clears a stale notReady once the precondition is met', async () => {
    // A prompt belongs to one attempt. Leaving it set would keep telling the
    // user to pick a strike they have already picked.
    readyTicket({ strike: null });
    await priceAndDrain();
    expect(useTreePricerStore.getState().notReady).toBeTruthy();

    // The strike is supplied WITHOUT touching the tree-pricer store, so only
    // executePricing can clear the prompt. Calling readyTicket() here instead
    // would reset notReady to null itself and the assertion below would hold
    // no matter what the pricer did -- a test that cannot fail.
    useCalculatorStore.setState({
      ticket: { ...useCalculatorStore.getState().ticket, strike: 580 } as never,
    });
    await priceAndDrain();

    expect(useTreePricerStore.getState().notReady).toBeNull();
  });
});
