/**
 * Saved scenarios: refusal routing, wire round-trip, and reopen fidelity.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Three properties are worth a permanent gate here, and they are the three
 * that would fail silently rather than loudly:
 *
 *   1. A refusal is routed by gRPC STATUS CODE, never by message text. The
 *      backend answers UNAUTHENTICATED (16) for "not signed in" and
 *      PERMISSION_DENIED (7) for "signed in, not Pro", and those lead to two
 *      different buttons. `entitlement.test.ts` already pins this rule for the
 *      calculator's own gate; the same rule has to hold here, so these tests
 *      deliberately VARY the message while holding the code fixed.
 *
 *   2. A saved scenario round-trips through the real generated message
 *      classes. This is where a mapping error hides: every field has a
 *      plausible zero, so a dropped premium or a transposed action produces a
 *      position that still prices, just not the one that was saved.
 *
 *   3. Saving sends exactly what pricing sends. Both go through
 *      `buildStrategyRequest`, and the assertion below reads the request the
 *      store actually handed the client rather than trusting that they share a
 *      function -- a future edit could reintroduce a second mapping.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  ListStrategiesResponse,
  SavedStrategy,
  SaveStrategyRequest,
  SaveStrategyResponse,
  StrategyRequest,
  Leg as ProtoLeg,
} from '../grpc/calculator_pb';

const GRPC_UNAUTHENTICATED = 16;
const GRPC_PERMISSION_DENIED = 7;

interface Outcome {
  ok?: unknown;
  fail?: { code: number; message: string };
}

const calls: { method: string; request: unknown }[] = [];
let listOutcome: Outcome = { ok: new ListStrategiesResponse() };
let saveOutcome: Outcome = { ok: new SaveStrategyResponse() };
let deleteOutcome: Outcome = { ok: {} };

function settleOutcome(method: string, req: unknown, o: Outcome) {
  calls.push({ method, request: req });
  return o.fail ? Promise.reject(o.fail) : Promise.resolve(o.ok);
}

vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    listStrategies = (req: unknown) => settleOutcome('listStrategies', req, listOutcome);
    saveStrategy = (req: unknown) => settleOutcome('saveStrategy', req, saveOutcome);
    deleteStrategy = (req: unknown) => settleOutcome('deleteStrategy', req, deleteOutcome);
    // Reopening a scenario fires a recalculation; it is not what these tests
    // assert, so it resolves to nothing rather than being left unscripted.
    calculateStrategy = () => Promise.reject({ code: 14, message: 'not scripted' });
    getMarketQuote = (_r: unknown, _m: unknown, cb: (e: unknown, r: unknown) => void) =>
      cb({ code: 14, message: 'not scripted' }, null);
    getMarketChain = (_r: unknown, _m: unknown, cb: (e: unknown, r: unknown) => void) =>
      cb({ code: 14, message: 'not scripted' }, null);
    getRiskFreeRate = (_r: unknown, _m: unknown, cb: (e: unknown, r: unknown) => void) =>
      cb({ code: 14, message: 'not scripted' }, null);
  },
}));

vi.mock('../lib/licence', () => ({
  authMetadata: () => ({}),
}));

vi.mock('../lib/supabase/client', () => ({
  createClient: () => ({
    auth: {
      getSession: async () => ({ data: { session: null } }),
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
    },
  }),
}));

import { useSavedScenariosStore } from './useSavedScenariosStore';
import { useCalculatorStore } from './useCalculatorStore';

/** A real, fully-populated wire scenario -- not a hand-built stand-in. */
function wireScenario(): SavedStrategy {
  const req = new StrategyRequest();
  req.setUnderlyingSymbol('SPY');
  req.setCurrentPrice(585.5);
  req.setImpliedVolatility(0.18);
  req.setRiskFreeRate(0.043);
  req.setDividendYield(0.013);

  const long = new ProtoLeg();
  long.setAction(ProtoLeg.Action.BUY);
  long.setType(ProtoLeg.Type.CALL);
  long.setStrike(580);
  long.setExpirationDays(30);
  long.setQuantity(2);
  long.setPremium(12.5);
  long.setImpliedVolatility(0.19);

  const short = new ProtoLeg();
  short.setAction(ProtoLeg.Action.SELL);
  short.setType(ProtoLeg.Type.PUT);
  short.setStrike(600);
  short.setExpirationDays(45);
  short.setQuantity(1);
  short.setPremium(5.25);
  short.setImpliedVolatility(0.17);

  req.setLegsList([long, short]);

  const saved = new SavedStrategy();
  saved.setId('abc-123');
  saved.setName('Earnings play');
  saved.setUpdatedAt('2026-08-24T12:00:00Z');
  saved.setRequest(req);
  return saved;
}

beforeEach(() => {
  calls.length = 0;
  listOutcome = { ok: new ListStrategiesResponse() };
  saveOutcome = { ok: new SaveStrategyResponse() };
  deleteOutcome = { ok: {} };
  useSavedScenariosStore.setState({
    scenarios: [],
    status: 'idle',
    failure: null,
    lastSavedName: null,
  });
});

describe('refusal routing is by status code, not message text', () => {
  it('routes UNAUTHENTICATED to needsSignIn, whatever the wording', async () => {
    for (const message of ['Sign in to save.', 'Saved scenarios belong to an account.', '']) {
      listOutcome = { fail: { code: GRPC_UNAUTHENTICATED, message } };
      await useSavedScenariosStore.getState().refresh();
      const { failure } = useSavedScenariosStore.getState();
      expect(failure?.needsSignIn).toBe(true);
      expect(failure?.needsPro).toBe(false);
    }
  });

  it('routes PERMISSION_DENIED to needsPro, whatever the wording', async () => {
    for (const message of ['Saving scenarios is a Pro feature.', 'Upgrade required.', '']) {
      listOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message } };
      await useSavedScenariosStore.getState().refresh();
      const { failure } = useSavedScenariosStore.getState();
      expect(failure?.needsPro).toBe(true);
      expect(failure?.needsSignIn).toBe(false);
    }
  });

  it('leaves an unknown code as neither, keeping the server sentence', async () => {
    listOutcome = { fail: { code: 14, message: 'Temporarily unavailable.' } };
    await useSavedScenariosStore.getState().refresh();
    const { failure } = useSavedScenariosStore.getState();
    expect(failure?.needsSignIn).toBe(false);
    expect(failure?.needsPro).toBe(false);
    expect(failure?.message).toBe('Temporarily unavailable.');
  });

  it('empties the list on refusal, so a signed-out user sees no stale names', async () => {
    const res = new ListStrategiesResponse();
    res.setStrategiesList([wireScenario()]);
    listOutcome = { ok: res };
    await useSavedScenariosStore.getState().refresh();
    expect(useSavedScenariosStore.getState().scenarios).toHaveLength(1);

    listOutcome = { fail: { code: GRPC_UNAUTHENTICATED, message: 'signed out' } };
    await useSavedScenariosStore.getState().refresh();
    expect(useSavedScenariosStore.getState().scenarios).toHaveLength(0);
  });
});

describe('wire round-trip', () => {
  it('maps every field of a saved scenario back off the wire', async () => {
    const res = new ListStrategiesResponse();
    res.setStrategiesList([wireScenario()]);
    listOutcome = { ok: res };

    await useSavedScenariosStore.getState().refresh();
    const [s] = useSavedScenariosStore.getState().scenarios;

    expect(s.id).toBe('abc-123');
    expect(s.name).toBe('Earnings play');
    expect(s.symbol).toBe('SPY');
    expect(s.updatedAt).toBe('2026-08-24T12:00:00Z');
    expect(s.spotPrice).toBeCloseTo(585.5, 10);
    expect(s.dividendYield).toBeCloseTo(0.013, 10);
    expect(s.legs).toHaveLength(2);

    // Asserted field by field, because every one of these has a plausible
    // zero that a dropped mapping would supply silently.
    expect(s.legs[0]).toMatchObject({
      action: 'BUY',
      option_type: 'CALL',
      strike_price: 580,
      quantity: 2,
      premium: 12.5,
      expiration_days: 30,
      implied_volatility: 0.19,
    });
    expect(s.legs[1]).toMatchObject({
      action: 'SELL',
      option_type: 'PUT',
      strike_price: 600,
      quantity: 1,
      premium: 5.25,
      expiration_days: 45,
      implied_volatility: 0.17,
    });
  });
});

describe('save', () => {
  beforeEach(() => {
    useCalculatorStore.setState({
      symbol: 'SPY',
      spotPrice: 585.5,
      riskFreeRate: 0.043,
      dividendYield: 0.013,
      legs: [
        {
          id: 'leg-1',
          instrument_type: 'OPTION',
          action: 'BUY',
          option_type: 'CALL',
          strike_price: 580,
          premium: 12.5,
          quantity: 2,
          expiration_days: 30,
          implied_volatility: 0.19,
        },
      ],
    });
  });

  it('sends the same request shape the pricing call builds', async () => {
    const ok = await useSavedScenariosStore.getState().save('  My scenario  ');
    expect(ok).toBe(true);

    const sent = calls.find((c) => c.method === 'saveStrategy')?.request as SaveStrategyRequest;
    // Trimmed, so a stray space cannot create a second scenario that looks
    // identical in the list.
    expect(sent.getName()).toBe('My scenario');

    const req = sent.getRequest()!;
    expect(req.getUnderlyingSymbol()).toBe('SPY');
    expect(req.getCurrentPrice()).toBeCloseTo(585.5, 10);
    expect(req.getRiskFreeRate()).toBeCloseTo(0.043, 10);
    expect(req.getDividendYield()).toBeCloseTo(0.013, 10);
    expect(req.getLegsList()).toHaveLength(1);
    expect(req.getLegsList()[0].getStrike()).toBe(580);
    expect(req.getLegsList()[0].getPremium()).toBe(12.5);
    // The multiplier is the engine's unit of account and is supplied by the
    // builder, not by the ticket -- 100 for a listed option.
    expect(req.getLegsList()[0].getContractMultiplier()).toBe(100);
  });

  it('refuses an empty name without calling the server', async () => {
    const ok = await useSavedScenariosStore.getState().save('   ');
    expect(ok).toBe(false);
    expect(calls.some((c) => c.method === 'saveStrategy')).toBe(false);
    expect(useSavedScenariosStore.getState().failure?.needsPro).toBe(false);
  });

  it('refuses a position with no legs without calling the server', async () => {
    useCalculatorStore.setState({ legs: [] });
    const ok = await useSavedScenariosStore.getState().save('Nothing');
    expect(ok).toBe(false);
    expect(calls.some((c) => c.method === 'saveStrategy')).toBe(false);
  });

  it('refuses rather than saving a fabricated zero risk-free rate', async () => {
    // null means "not measured yet". Saving 0 in its place would store a
    // scenario that reprices differently the moment it is reopened.
    useCalculatorStore.setState({ riskFreeRate: null });
    const ok = await useSavedScenariosStore.getState().save('Too early');
    expect(ok).toBe(false);
    expect(calls.some((c) => c.method === 'saveStrategy')).toBe(false);
    expect(useSavedScenariosStore.getState().failure?.message).toMatch(/risk-free rate/i);
  });

  it('surfaces a Pro refusal from the save call itself', async () => {
    saveOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Pro only' } };
    const ok = await useSavedScenariosStore.getState().save('Blocked');
    expect(ok).toBe(false);
    expect(useSavedScenariosStore.getState().failure?.needsPro).toBe(true);
  });
});

describe('delete', () => {
  it('drops the row locally even before the refresh lands', async () => {
    const res = new ListStrategiesResponse();
    res.setStrategiesList([wireScenario()]);
    listOutcome = { ok: res };
    await useSavedScenariosStore.getState().refresh();
    expect(useSavedScenariosStore.getState().scenarios).toHaveLength(1);

    // The refresh after the delete returns an empty list, as the server would.
    listOutcome = { ok: new ListStrategiesResponse() };
    const ok = await useSavedScenariosStore.getState().remove('abc-123');
    expect(ok).toBe(true);
    expect(useSavedScenariosStore.getState().scenarios).toHaveLength(0);
  });
});

describe('apply', () => {
  it('reopens a scenario into the calculator store', async () => {
    const res = new ListStrategiesResponse();
    res.setStrategiesList([wireScenario()]);
    listOutcome = { ok: res };
    await useSavedScenariosStore.getState().refresh();
    const [scenario] = useSavedScenariosStore.getState().scenarios;

    useSavedScenariosStore.getState().apply(scenario);

    const calc = useCalculatorStore.getState();
    expect(calc.symbol).toBe('SPY');
    expect(calc.spotPrice).toBeCloseTo(585.5, 10);
    expect(calc.dividendYield).toBeCloseTo(0.013, 10);
    // Both legs land, and they land with the values that were saved -- the
    // whole point of reopening.
    expect(calc.legs).toHaveLength(2);
    expect(calc.legs[0]).toMatchObject({ action: 'BUY', option_type: 'CALL', strike_price: 580, premium: 12.5 });
    expect(calc.legs[1]).toMatchObject({ action: 'SELL', option_type: 'PUT', strike_price: 600, premium: 5.25 });
    // Ids are reassigned by the calculator store's own counter, never
    // persisted -- two legs must not share one.
    expect(calc.legs[0].id).not.toBe(calc.legs[1].id);
  });
});
