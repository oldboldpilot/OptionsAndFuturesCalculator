/**
 * An Asian leg must survive the whole client path, and a vanilla one must be
 * byte-identical to what it was before this field existed.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `calculator.proto`'s `Leg` gained `asian_type` (field 9, zero = NOT_ASIAN),
 * and the engine REFUSES a position holding an Asian leg with
 * FAILED_PRECONDITION rather than pricing it against terminal spot. That
 * refusal is only honest if the field actually reaches the engine: a client
 * that quietly dropped it would send a vanilla request, receive a confident
 * vanilla answer, and render it under a ticket that said "Avg price". The
 * dropped field would be invisible in every number on screen -- which is the
 * exact shape of the `?? 0` expiry defect this suite was created for.
 *
 * So the assertions here are about what is SENT, read off the real generated
 * `StrategyRequest` (only the transport is faked), and about what `commitTicket`
 * stores. The gRPC status code, never the message text, is what the store
 * discriminates on -- pinned in model-limit.test.ts, not restated here.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  strategyResponse,
  settle,
  GRPC_FAILED_PRECONDITION,
} from '../test/grpc-harness';
import { Leg as ProtoLeg, StrategyRequest } from '../grpc/calculator_pb';

let nextStrategy:
  | { ok: ReturnType<typeof strategyResponse> }
  | { fail: { code: number; message: string } } = { ok: strategyResponse() };

const fake = createFakeClient({
  getMarketChain: () =>
    ({
      ok: chainResponse(
        [chainExpiration('2026-08-17', 7), chainExpiration('2026-08-24', 14)],
        [chainStrike(580, { isAtm: true, callBid: 4.5, callAsk: 4.68, callIv: 0.22 })],
        '2026-08-17',
      ),
    }),
  calculateStrategy: () => nextStrategy,
});

// vi.mock is hoisted, so every test file declares its own block at top level.
// There is deliberately no shared helper -- see the note at the foot of
// src/test/grpc-harness.ts for why wrapping these in a function would apply
// them to the harness instead of to the caller.
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
import { useTreePricerStore } from './useTreePricerStore';

function resetStore() {
  fake.calls.length = 0;
  nextStrategy = { ok: strategyResponse() };
  useCalculatorStore.setState({
    symbol: 'SPY',
    legs: [],
    spotPrice: 590,
    riskFreeRate: 0.0385,
    rateSource: 'measured',
    result: null,
    error: null,
    gateDenied: null,
    modelLimit: null,
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

async function loadReadyChain() {
  useCalculatorStore.getState().loadChain('2026-08-17');
  await settle(() => useCalculatorStore.getState().chainStatus === 'ready', 'chain ready');
}

/** The legs of the last CalculateStrategy request the store actually sent. */
function sentLegs(): ProtoLeg[] {
  const call = [...fake.calls].reverse().find((c) => c.method === 'calculateStrategy');
  if (!call) throw new Error('no calculateStrategy call was made');
  return (call.request as StrategyRequest).getLegsList();
}

describe('the ticket composes an averaging style, and it reaches the wire', () => {
  beforeEach(() => {
    resetStore();
  });

  // The default. A trader who never touches the control meant a vanilla
  // option, and NOT_ASIAN is also the proto's zero value, so the default
  // costs nothing on the wire and cannot change an existing answer.
  it('defaults the ticket to Vanilla', () => {
    expect(useCalculatorStore.getState().ticket.asianType).toBe('NOT_ASIAN');
  });

  it('commits the ticket default onto the leg as NOT_ASIAN', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6 });
    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.error).toBeNull();
    expect(st.legs).toHaveLength(1);
    expect(st.legs[0].asian_type).toBe('NOT_ASIAN');
  });

  // BREAK DIRECTION for the commit half: drop `asian_type: t.asianType` from
  // commitTicket and this fails on `undefined`, which is precisely the state
  // in which a ticket says "Avg price" and the position holds a vanilla.
  it('carries an AVERAGE_PRICE ticket onto the committed leg', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({
      strike: 580,
      premium: 4.6,
      impliedVolatility: 0.22,
      asianType: 'AVERAGE_PRICE',
    });
    useCalculatorStore.getState().commitTicket();

    const st = useCalculatorStore.getState();
    expect(st.legs).toHaveLength(1);
    expect(st.legs[0].asian_type).toBe('AVERAGE_PRICE');
  });

  it('carries an AVERAGE_STRIKE ticket onto the committed leg', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({
      strike: 580,
      premium: 4.6,
      impliedVolatility: 0.22,
      asianType: 'AVERAGE_STRIKE',
    });
    useCalculatorStore.getState().commitTicket();

    expect(useCalculatorStore.getState().legs[0].asian_type).toBe('AVERAGE_STRIKE');
  });

  // BREAK DIRECTION for the wire half, and the load-bearing test in this file:
  // delete the `pLeg.setAsianType(...)` line in calculateStrategy and this
  // fails with 0 (NOT_ASIAN) against 1. The request inspected here is a REAL
  // generated StrategyRequest -- the fake replaces the transport only -- so
  // this reads the same field the engine would.
  it('sends AVERAGE_PRICE on the wire, as the proto enum value', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({
      strike: 580,
      premium: 4.6,
      impliedVolatility: 0.22,
      asianType: 'AVERAGE_PRICE',
    });
    useCalculatorStore.getState().commitTicket();

    nextStrategy = { fail: { code: GRPC_FAILED_PRECONDITION, message: 'Asian leg.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const legs = sentLegs();
    expect(legs).toHaveLength(1);
    expect(legs[0].getAsianType()).toBe(ProtoLeg.AsianType.AVERAGE_PRICE);
    // And the refusal the engine gives back for it lands in modelLimit, not
    // in error -- the two halves have to hold together for the panel copy to
    // be about the leg the user actually built.
    expect(useCalculatorStore.getState().modelLimit).toBe('Asian leg.');
    expect(useCalculatorStore.getState().error).toBeNull();
  });

  it('sends AVERAGE_STRIKE on the wire as its own distinct value', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({
      strike: 580,
      premium: 4.6,
      impliedVolatility: 0.22,
      asianType: 'AVERAGE_STRIKE',
    });
    useCalculatorStore.getState().commitTicket();

    nextStrategy = { fail: { code: GRPC_FAILED_PRECONDITION, message: 'Asian leg.' } };
    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().isLoading === false, 'settled');

    const legs = sentLegs();
    // Asserted against AVERAGE_STRIKE specifically, and against AVERAGE_PRICE
    // NOT being what was sent: a mapping that collapsed both styles onto one
    // value would satisfy "it is Asian" while pricing the wrong instrument.
    expect(legs[0].getAsianType()).toBe(ProtoLeg.AsianType.AVERAGE_STRIKE);
    expect(legs[0].getAsianType()).not.toBe(ProtoLeg.AsianType.AVERAGE_PRICE);
  });

  // THE REGRESSION GUARD the spec names as its gate: a vanilla-only position
  // must be identical to what it was before this field existed. Without this,
  // "the field reaches the wire" is satisfied by a client that marks every leg
  // Asian and takes the whole product down with a refusal.
  it('leaves a vanilla position at NOT_ASIAN and still computes it', async () => {
    await loadReadyChain();
    useCalculatorStore.getState().setTicket({ strike: 580, premium: 4.6, impliedVolatility: 0.22 });
    useCalculatorStore.getState().commitTicket();

    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().result !== null, 'result');

    const legs = sentLegs();
    expect(legs[0].getAsianType()).toBe(ProtoLeg.AsianType.NOT_ASIAN);
    const st = useCalculatorStore.getState();
    expect(st.modelLimit).toBeNull();
    expect(st.error).toBeNull();
    expect(st.result).not.toBeNull();
  });

  // A leg built by a path that never heard of averaging -- the strategy
  // templates in StrategySelector still call addLeg without the field -- must
  // go on the wire as NOT_ASIAN rather than throwing or sending undefined.
  it('treats a leg with no asian_type at all as NOT_ASIAN', async () => {
    useCalculatorStore.getState().addLeg({
      instrument_type: 'INSTRUMENT_EQUITY_OPTION',
      action: 'BUY',
      option_type: 'CALL',
      strike_price: 580,
      premium: 7.25,
      quantity: 1,
      expiration_days: 30,
      implied_volatility: 0.2,
    });
    expect(useCalculatorStore.getState().legs[0].asian_type).toBeUndefined();

    await useCalculatorStore.getState().calculateStrategy();
    await settle(() => useCalculatorStore.getState().result !== null, 'result');

    expect(sentLegs()[0].getAsianType()).toBe(ProtoLeg.AsianType.NOT_ASIAN);
  });
});

/**
 * The exercise-style default, pinned.
 *
 * Every listed US equity option -- every contract the chain beside the tree
 * panel quotes -- is American-style, so opening on European named the one
 * style the instrument in the ticket is not. This test exists because a
 * default is a decision nobody reads again: it lives in one initialiser, it
 * has no call site, and nothing else in the suite would notice it moving.
 *
 * BREAK DIRECTION: set `exerciseType` back to 'EUROPEAN' in
 * useTreePricerStore and this fails.
 */
describe('tree pricer defaults', () => {
  it('defaults the exercise style to AMERICAN', () => {
    expect(useTreePricerStore.getState().exerciseType).toBe('AMERICAN');
  });

  // The averaging default is NOT changed by that decision, and the two are
  // independent axes of the same contract: an American vanilla is the common
  // instrument, an American Asian is not.
  it('still defaults averaging to Vanilla', () => {
    expect(useTreePricerStore.getState().asianType).toBe('NOT_ASIAN');
  });
});
