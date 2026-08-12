/**
 * Preconditions in useCalculatorStore are not errors.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `useCalculatorStore` has `notReady: string | null` separate from `error`.
 * Preconditions (no strike / no premium / no expiry in commitTicket; no spot /
 * no IV / no expiry-days in calculateStrategy) set `notReady` and clear `error`.
 * Genuine failures (RPC errors, and the Treasury-rate feed failure) still set
 * `error`.
 *
 * These tests pin the ROUTING, not the wording. Precondition messages arriving
 * in `error` render as "Unavailable" in loss red across panels, which is a
 * regression.
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
  GRPC_UNAVAILABLE,
  GRPC_PERMISSION_DENIED,
  GRPC_FAILED_PRECONDITION,
} from '../test/grpc-harness';

let nextOutcome:
  | { ok: ReturnType<typeof strategyResponse> }
  | { fail: { code: number; message: string } } = {
  ok: strategyResponse(),
};
let rateFails = false;
let quoteFails = false;
let quoteSideEffect: (() => void) | null = null;
let rateSideEffect: (() => void) | null = null;
let strategySideEffect: (() => void) | null = null;

const fake = createFakeClient({
  getMarketQuote: () => {
    // Side effect BEFORE the outcome is returned: createFakeClient calls this
    // handler and only then invokes the callback, which is the seam that lets
    // a test act while an RPC is in flight.
    if (quoteSideEffect) quoteSideEffect();
    return quoteFails
      ? { fail: { code: GRPC_UNAVAILABLE, message: 'no quote' } }
      : { ok: quoteResponse(580) };
  },
  getMarketChain: () => ({
    ok: chainResponse(
      [chainExpiration('2026-08-17', 7), chainExpiration('2026-08-24', 14)],
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
    if (strategySideEffect) strategySideEffect();
    return nextOutcome;
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

const goodLeg = {
  id: 'leg-notready-1',
  instrument_type: 'INSTRUMENT_EQUITY_OPTION',
  action: 'BUY',
  option_type: 'CALL',
  strike_price: 580,
  premium: 4.5,
  quantity: 1,
  expiration_days: 7,
  implied_volatility: 0.2,
};

function resetStore() {
  rateFails = false;
  rateSideEffect = null;
  strategySideEffect = null;
  // Reset HERE, not at the end of the test that sets them: a settle() timeout
  // throws before any cleanup line, and a live quoteSideEffect calls
  // commitTicket and blanks chainExpirations for every test after it.
  quoteFails = false;
  quoteSideEffect = null;
  nextOutcome = { ok: strategyResponse() };
  fake.calls.length = 0;
  useCalculatorStore.setState({
    symbol: 'SPY',
    assetClass: 'EQUITY',
    legs: [],
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
    chainStrikes: [chainStrike(580, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.2 }) as never],
    chainExpirations: [{ date: '2026-08-17', dte: 7, label: '2026-08-17' }],
    selectedExpiration: '2026-08-17',
    chainStatus: 'ready',
    chainError: null,
    ticket: {
      action: 'BUY',
      optionType: 'CALL',
      expiration: '2026-08-17',
      strike: 580,
      premium: 4.5,
      quantity: 1,
      impliedVolatility: 0.2,
      asianType: 'NOT_ASIAN',
    },
  });
}

describe('calculator store notReady routing', () => {
  beforeEach(() => {
    resetStore();
  });

  describe('1. commitTicket preconditions', () => {
    it('routes missing strike to notReady (error null, gateDenied null)', () => {
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: null },
      });
      useCalculatorStore.getState().commitTicket();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.gateDenied).toBeNull();
    });

    it('routes missing premium to notReady (error null, gateDenied null)', () => {
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: 580, premium: null },
      });
      useCalculatorStore.getState().commitTicket();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.gateDenied).toBeNull();
    });

    it('routes missing/unmatched expiry to notReady (error null, gateDenied null)', () => {
      useCalculatorStore.setState({
        selectedExpiration: '',
        ticket: {
          ...useCalculatorStore.getState().ticket,
          strike: 580,
          premium: 4.5,
          expiration: '2099-01-01',
        },
      });
      useCalculatorStore.getState().commitTicket();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.gateDenied).toBeNull();
    });
  });

  describe('2. calculateStrategy preconditions', () => {
    it('routes spotPrice 0 to notReady (error null, result null)', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 0 });
      await useCalculatorStore.getState().calculateStrategy();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.result).toBeNull();
    });

    it('routes missing implied volatility to notReady (error null, result null)', async () => {
      useCalculatorStore.setState({
        legs: [{ ...goodLeg, implied_volatility: undefined }],
        spotPrice: 580,
      });
      await useCalculatorStore.getState().calculateStrategy();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.result).toBeNull();
    });

    it('routes days <= 0 to notReady (error null, result null)', async () => {
      useCalculatorStore.setState({
        legs: [{ ...goodLeg, expiration_days: 0 }],
        spotPrice: 580,
      });
      await useCalculatorStore.getState().calculateStrategy();

      const st = useCalculatorStore.getState();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
      expect(st.result).toBeNull();
    });
  });

  describe('3. both directions: genuine RPC failure', () => {
    it('routes scripted RPC failure (non-7, non-9) to error truthy AND notReady null', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
      nextOutcome = { fail: { code: GRPC_UNAVAILABLE, message: 'Backend engine unavailable' } };

      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.error).toBeTruthy();
      expect(st.error).toBe('Backend engine unavailable');
      expect(st.notReady).toBeNull();
    });
  });

  describe('4. Treasury-rate guard', () => {
    it('lands in error, not notReady', async () => {
      rateFails = true;
      useCalculatorStore.setState({
        legs: [goodLeg],
        spotPrice: 580,
        riskFreeRate: null,
        rateSource: 'pending',
        result: null,
        error: null,
        notReady: null,
      });

      await useCalculatorStore.getState().calculateStrategy();

      const st = useCalculatorStore.getState();
      expect(st.error).toBeTruthy();
      expect(st.error).toContain('Treasury feed is unavailable');
      expect(st.notReady).toBeNull();
    });
  });

  describe('5. stale prompt clearance', () => {
    it('clears a stale commitTicket notReady prompt once precondition is met', () => {
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: null },
      });
      useCalculatorStore.getState().commitTicket();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();

      // Supply the missing input without resetting notReady manually
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: 580, premium: 4.5 },
      });
      useCalculatorStore.getState().commitTicket();

      expect(useCalculatorStore.getState().notReady).toBeNull();
    });

    it('clears a stale calculateStrategy notReady prompt once precondition is met', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 0 });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();

      // Supply the missing input without resetting notReady manually
      useCalculatorStore.setState({ spotPrice: 580 });
      await useCalculatorStore.getState().calculateStrategy();

      expect(useCalculatorStore.getState().notReady).toBeNull();
    });
  });

  describe('6. clearLegs and removeLeg down to zero', () => {
    it('clearLegs() nulls notReady', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 0 });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();

      useCalculatorStore.getState().clearLegs();

      const st = useCalculatorStore.getState();
      expect(st.legs).toHaveLength(0);
      expect(st.notReady).toBeNull();
    });

    it('removeLeg down to zero nulls notReady', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 0 });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();

      useCalculatorStore.getState().removeLeg(goodLeg.id);

      const st = useCalculatorStore.getState();
      expect(st.legs).toHaveLength(0);
      expect(st.notReady).toBeNull();
    });
  });

  describe('7. commitTicket precondition does not destroy a live result', () => {
    it('calculates successfully, then commits with no strike; result remains non-null AND notReady truthy', async () => {
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().result !== null, 'result');

      expect(useCalculatorStore.getState().result).not.toBeNull();

      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: null },
      });
      useCalculatorStore.getState().commitTicket();

      const st = useCalculatorStore.getState();
      expect(st.result).not.toBeNull();
      expect(st.notReady).toBeTruthy();
      expect(st.error).toBeNull();
    });
  });

  /**
   * One refusal on screen at a time, in BOTH directions.
   *
   * Every `notReady` write clears `error`. The reverse was missing, so the two
   * could stand together: the loss-red chip beside the neutral prompt, with
   * all five panels showing "Unavailable" -- and `error` is checked before
   * `result`, so a live position is blanked as well.
   *
   * The interleaving is the whole defect, and it IS expressible here. The fake
   * client calls `handler(req)` BEFORE it invokes the callback, and the handler
   * is a per-test closure, so a side-effecting handler runs exactly between
   * setSymbol's entry clear and the quote failure landing. (An earlier version
   * of this file claimed covering this needed a deferred outcome in the shared
   * harness. That was wrong, and a false recorded reason is worse than a
   * missing test.)
   */
  it('a failing quote clears a notReady raised while the quote was in flight', async () => {
    quoteFails = true;
    quoteSideEffect = () => {
      // Runs INSIDE the quote RPC, after setSymbol cleared both fields and
      // before the failure is delivered. chainExpirations is empty after a
      // symbol switch, so commitTicket falls to the expiry guard.
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: 580, premium: 4.68 } as never,
        chainExpirations: [],
      });
      useCalculatorStore.getState().commitTicket();
    };

    useCalculatorStore.getState().setSymbol('XYZ');
    await settle(() => useCalculatorStore.getState().error !== null, 'quote failed');

    quoteFails = false;
    quoteSideEffect = null;

    const st = useCalculatorStore.getState();
    expect(st.error).toBeTruthy();
    expect(st.notReady).toBeNull();
  });

  /**
   * The OTHER half of the invariant, which nothing pinned.
   *
   * "Every notReady write clears `error`" was stated in the store and asserted
   * by no test: dropping `error: null` from the commitTicket and spot guards
   * left the whole suite green, because every `expect(error).toBeNull()` ran
   * from a reset that had already nulled it. Needs no interleaving -- a failed
   * quote sets `error`, and pressing Add without a strike must clear it.
   *
   * Without the clear, `error` outranks `notReady` in all five panels, so the
   * Add press appears to do nothing while the panels keep saying "Unavailable".
   */
  it('raising a notReady clears a standing error', async () => {
    quoteFails = true;
    useCalculatorStore.getState().setSymbol('XYZ');
    await settle(() => useCalculatorStore.getState().error !== null, 'error standing');
    quoteFails = false;

    useCalculatorStore.setState({
      ticket: { ...useCalculatorStore.getState().ticket, strike: null } as never,
    });
    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.notReady).toBeTruthy();
    expect(st.error).toBeNull();
  });

  /**
   * The Treasury guard sits AFTER an await, so calculateStrategy's entry clear
   * does not cover it -- a notReady raised while the rate request is in flight
   * would otherwise stand beside the outage message. Proven reachable, not
   * defensive.
   */
  it('the Treasury-rate failure clears a notReady raised while it was in flight', async () => {
    rateFails = true;
    rateSideEffect = () => {
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: null } as never,
      });
      useCalculatorStore.getState().commitTicket();
    };

    useCalculatorStore.setState({
      legs: [goodLeg],
      spotPrice: 580,
      riskFreeRate: null,
      rateSource: 'pending',
    });
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const st = useCalculatorStore.getState();
    expect(st.error).toBeTruthy();
    expect(st.notReady).toBeNull();
  });

  /**
   * Same shape for the RPC catch: it is after the await too.
   */
  it('an RPC failure clears a notReady raised while the call was in flight', async () => {
    nextOutcome = { fail: { code: GRPC_UNAVAILABLE, message: 'engine down' } };
    useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
    strategySideEffect = () => {
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: null } as never,
      });
      useCalculatorStore.getState().commitTicket();
    };

    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const st = useCalculatorStore.getState();
    expect(st.error).toBeTruthy();
    expect(st.notReady).toBeNull();
  });

  /**
   * The first half of the invariant, pinned across EVERY guard rather than one.
   *
   * A single-guard test catches a wholesale refactor and misses a targeted edit
   * to one branch — and deleting `error: null` from the other five left the
   * whole suite green. Each case here stands a real `error` up first (via a
   * failed quote, not by hand) and then drives one guard.
   */
  describe('every notReady guard clears a standing error', () => {
    async function standingError() {
      quoteFails = true;
      useCalculatorStore.getState().setSymbol('XYZ');
      await settle(() => useCalculatorStore.getState().error !== null, 'error standing');
      quoteFails = false;
      expect(useCalculatorStore.getState().error).toBeTruthy();
    }

    it('commitTicket: no premium', async () => {
      await standingError();
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: 580, premium: null } as never,
      });
      useCalculatorStore.getState().commitTicket();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();
      expect(useCalculatorStore.getState().error).toBeNull();
    });

    it('commitTicket: no expiry', async () => {
      await standingError();
      useCalculatorStore.setState({
        ticket: { ...useCalculatorStore.getState().ticket, strike: 580, premium: 4.68 } as never,
        chainExpirations: [],
      });
      useCalculatorStore.getState().commitTicket();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();
      expect(useCalculatorStore.getState().error).toBeNull();
    });

    it('calculateStrategy: no implied volatility', async () => {
      await standingError();
      useCalculatorStore.setState({
        legs: [{ ...goodLeg, implied_volatility: 0 }] as never,
        spotPrice: 580,
      });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();
      expect(useCalculatorStore.getState().error).toBeNull();
    });

    it('calculateStrategy: no expiration days', async () => {
      await standingError();
      useCalculatorStore.setState({
        legs: [{ ...goodLeg, expiration_days: 0 }] as never,
        spotPrice: 580,
      });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();
      expect(useCalculatorStore.getState().error).toBeNull();
    });

    it('calculateStrategy: no spot price', async () => {
      await standingError();
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 0 });
      await useCalculatorStore.getState().calculateStrategy();
      expect(useCalculatorStore.getState().notReady).toBeTruthy();
      expect(useCalculatorStore.getState().error).toBeNull();
    });
  });

  /**
   * The remaining exits from the in-flight window, which nothing pinned.
   *
   * `calculateStrategy` clears notReady at entry, then awaits twice. Its three
   * FAILING exits are covered above; SUCCESS and both entitlement branches
   * were not, and dropping their clears left the suite green.
   */
  describe('every exit from the in-flight window clears notReady', () => {
    function raiseNotReadyMidFlight() {
      strategySideEffect = () => {
        useCalculatorStore.setState({
          ticket: { ...useCalculatorStore.getState().ticket, strike: null } as never,
        });
        useCalculatorStore.getState().commitTicket();
      };
    }

    it('success', async () => {
      nextOutcome = { ok: strategyResponse() };
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
      raiseNotReadyMidFlight();

      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.result).not.toBeNull();
      expect(st.notReady).toBeNull();
    });

    it('FAILED_PRECONDITION routes to modelLimit and still clears notReady', async () => {
      nextOutcome = { fail: { code: GRPC_FAILED_PRECONDITION, message: 'Asian legs are not priced.' } };
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
      raiseNotReadyMidFlight();

      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.modelLimit).toBeTruthy();
      // Without the clear, the panel reads "Not modelled" while TopBar shows a
      // stale "Pick a strike before adding the leg." chip beside it.
      expect(st.notReady).toBeNull();
    });

    it('PERMISSION_DENIED routes to gateDenied and still clears notReady', async () => {
      nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro.' } };
      useCalculatorStore.setState({ legs: [goodLeg], spotPrice: 580 });
      raiseNotReadyMidFlight();

      await useCalculatorStore.getState().calculateStrategy();
      await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

      const st = useCalculatorStore.getState();
      expect(st.gateDenied).toBeTruthy();
      expect(st.notReady).toBeNull();
    });
  });
});
