/**
 * Applying a parse must move the user forward without inventing anything.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `StrategyParams` carries a symbol, an asset class, a structure, an expiry in
 * DAYS and a contract count. It carries no strike and no premium, and those are
 * the two numbers that decide every payoff figure downstream. So applying sets
 * exactly what was parsed and stops: the legs still come off the live chain
 * through the picker's own Apply, exactly as they do for a hand-picked
 * strategy. A store that filled in a strike here would be doing the one thing
 * this codebase spends the most effort not doing.
 *
 * Two conversions are where the honesty actually lives:
 *
 *   - The expiry. The model says "45 days"; a chain lists DATES, and there is
 *     rarely one exactly that far out. Snapping to the nearest is right, and
 *     pricing 44 days as though it were the 45 the trader said without saying
 *     so is not.
 *   - The vocabulary. `asset_class` and `strategy` are free strings on the
 *     wire. A value outside what this build offers is refused BY NAME, never
 *     coerced to the nearest-looking one -- coercing an asset class is how a
 *     futures root gets priced off an equity quote of the same ticker.
 *
 * BREAK DIRECTIONS, all four run and observed:
 *   - `nearestListedExpiry` returning `expirations[0]` -> the nearest-expiry
 *     tests fail (the fixture's first entry is deliberately not the nearest).
 *   - dropping the asset-class and catalogue guards in `applyParams` -> the
 *     two refusal tests fail, and the symbol moves when it must not.
 *   - having apply build legs -> the "invents nothing" test fails.
 *   - having `stepDemo` populate `params` from the recorded array -> the
 *     recorded/live separation test fails.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  chainExpiration,
  chainStrike,
  chainResponse,
  quoteResponse,
  rateResponse,
  parseParams,
  settle,
  type FakeParseResponse,
} from '../test/grpc-harness';

let nextParse: { ok: FakeParseResponse } | { fail: { code: number; message: string } } = {
  ok: parseParams({
    symbol: 'NVDA',
    assetClass: 'EQUITY',
    strategy: 'bull_call_spread',
    expirationDays: 45,
    quantity: 2,
  }),
};

/**
 * The listed expirations every test in this file snaps against.
 *
 * The first entry is deliberately NOT the nearest to any target used below: an
 * implementation that returned `expirations[0]` and called it "nearest" would
 * otherwise pass on a fixture whose first entry happened to win.
 */
const LISTED = [
  chainExpiration('2026-08-14', 3),
  chainExpiration('2026-09-18', 38),
  chainExpiration('2026-09-25', 45),
  chainExpiration('2026-10-16', 66),
];

/**
 * The same expirations in the shape the STORE holds them, derived from the
 * wire fixtures above rather than restated. `nearestListedExpiry` takes the
 * mapped `ChainExpiration`, not the generated message, and a second hand-typed
 * copy is how the pure function would come to be tested against a chain the
 * store never produces.
 */
const LISTED_AS_STORED = LISTED.map((e) => ({
  date: e.getDateStr(),
  dte: e.getDaysToExpiry(),
  label: e.getLabel(),
}));

const fakeCalc = createFakeClient({
  getMarketQuote: () => ({ ok: quoteResponse(182.5) }),
  getRiskFreeRate: () => ({ ok: rateResponse(0.0385) }),
  getMarketChain: () => ({
    ok: chainResponse(LISTED, [chainStrike(180, { isAtm: true }), chainStrike(190)], '2026-09-18'),
  }),
});

const fakeAssistant = createFakeClient({
  parseStrategy: () => nextParse,
});

vi.mock('../grpc/CalculatorServiceClientPb', () => ({
  OptionsCalculatorClient: class {
    getMarketQuote = fakeCalc.client.getMarketQuote;
    getRiskFreeRate = fakeCalc.client.getRiskFreeRate;
    getMarketChain = fakeCalc.client.getMarketChain;
    calculateStrategy = fakeCalc.client.calculateStrategy;
  },
}));

vi.mock('../grpc/AssistantServiceClientPb', () => ({
  StrategyAssistantClient: class {
    parseStrategy = fakeAssistant.client.parseStrategy;
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

import { useAssistantStore, nearestListedExpiry } from './useAssistantStore';
import { useCalculatorStore } from './useCalculatorStore';
import { RECORDED_EXCHANGES } from '../config/assistantExamples';

/**
 * The catalogue as the picker offers it, passed in exactly as `AssistantPanel`
 * passes it. Kept small and explicit rather than imported from the component:
 * what is under test is the guard, not the list.
 */
const KNOWN = {
  ids: ['bull_call_spread', 'iron_condor', 'long_call', 'calendar_spread', 'futures_long'],
  multiExpiry: ['calendar_spread'],
};

async function parseThen(response: FakeParseResponse) {
  nextParse = { ok: response };
  useAssistantStore.setState({ utterance: 'anything' });
  await useAssistantStore.getState().parse();
  await settle(() => useAssistantStore.getState().params !== null, 'parsed');
}

describe('applying a parse', () => {
  beforeEach(() => {
    useAssistantStore.getState().reset();
    useCalculatorStore.setState({
      symbol: 'SPY',
      assetClass: 'EQUITY',
      legs: [],
      spotPrice: 0,
      chainStrikes: [],
      chainExpirations: [],
      futuresCurve: [],
      selectedExpiration: '',
      chainStatus: 'idle',
    });
    fakeCalc.calls.length = 0;
  });

  it('sets symbol, asset class, contract count and structure — and no legs', async () => {
    await parseThen(
      parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 45,
        quantity: 2,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    const calc = useCalculatorStore.getState();
    expect(calc.symbol).toBe('NVDA');
    expect(calc.assetClass).toBe('EQUITY');
    expect(calc.ticket.quantity).toBe(2);
    expect(useAssistantStore.getState().selectedStrategyId).toBe('bull_call_spread');
    expect(useAssistantStore.getState().applyBlocked).toBeNull();

    // The whole point. A parse names no strike and no premium, so applying one
    // must leave the position empty for the chain to fill.
    expect(calc.legs).toHaveLength(0);
  });

  it('snaps the stated expiry onto the NEAREST listed one and says which', async () => {
    await parseThen(
      parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 40,
        quantity: 1,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    // 40 days lands between the 38d and 45d contracts; 38 is nearer, and it is
    // neither the first listed nor the one the chain opened on.
    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-09-18');
    const note = useAssistantStore.getState().applyNote ?? '';
    expect(note).toContain('2026-09-18');
    expect(note).toContain('38d');
    // Stated as an approximation, because it is one.
    expect(note).toContain('40d');
  });

  it('still names the date when the stated expiry is listed exactly', async () => {
    await parseThen(
      parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 45,
        quantity: 1,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    expect(useCalculatorStore.getState().selectedExpiration).toBe('2026-09-25');
    // A trader said a number of DAYS and the position is priced to a DATE.
    // Which date that is has to be visible without opening the ticket, even
    // when nothing was rounded.
    expect(useAssistantStore.getState().applyNote ?? '').toContain('2026-09-25');
  });

  it('refuses an asset class this calculator does not price, and moves nothing', async () => {
    await parseThen(
      parseParams({
        symbol: 'XAU',
        assetClass: 'COMMODITY',
        strategy: 'long_call',
        expirationDays: 30,
        quantity: 1,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    const st = useAssistantStore.getState();
    expect(st.applyBlocked).toContain('COMMODITY');
    expect(st.selectedStrategyId).toBeNull();
    // Coercing to the nearest-looking class is how the ES / Eversource
    // collision would be reached from the client instead of the server.
    expect(useCalculatorStore.getState().symbol).toBe('SPY');
  });

  it('refuses a structure the picker does not offer, and moves nothing', async () => {
    // `crack_321` is gated out of the picker deliberately: the engine prices
    // it, the assistant was never taught it. A parse naming it points at an
    // entry the user cannot select, and saying so beats selecting nothing.
    await parseThen(
      parseParams({
        symbol: 'CL',
        assetClass: 'FUTURES',
        strategy: 'crack_321',
        expirationDays: 30,
        quantity: 1,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    const st = useAssistantStore.getState();
    expect(st.applyBlocked).toContain('crack_321');
    expect(st.selectedStrategyId).toBeNull();
    expect(useCalculatorStore.getState().symbol).toBe('SPY');
  });

  it('says a two-expiry structure only got one leg from the parse', async () => {
    await parseThen(
      parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'calendar_spread',
        expirationDays: 38,
        quantity: 1,
        farExpirationDays: 66,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);

    const note = useAssistantStore.getState().applyNote ?? '';
    expect(note).toContain('two expiries');
    expect(note).toContain('66');
    // Applied anyway, not refused: the picker marks it as needing a second
    // chain and the engine prices it once both legs exist.
    expect(useAssistantStore.getState().selectedStrategyId).toBe('calendar_spread');
  });

  it('applies the same parse twice as two distinct events', async () => {
    await parseThen(
      parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 45,
        quantity: 1,
      }),
    );
    useAssistantStore.getState().applyParams(KNOWN);
    const first = useAssistantStore.getState().applySeq;
    useAssistantStore.getState().applyParams(KNOWN);

    // The id does not change, so the picker has nothing to react to unless the
    // sequence does. Without this, a user who applied, picked something else
    // by hand, then pressed Apply again would watch the button do nothing.
    expect(useAssistantStore.getState().applySeq).toBe(first + 1);
  });
});

describe('nearestListedExpiry', () => {
  it('picks the nearest, not the first or the last', () => {
    expect(nearestListedExpiry(LISTED_AS_STORED, 40)?.date).toBe('2026-09-18');
    expect(nearestListedExpiry(LISTED_AS_STORED, 44)?.date).toBe('2026-09-25');
    expect(nearestListedExpiry(LISTED_AS_STORED, 1)?.date).toBe('2026-08-14');
    expect(nearestListedExpiry(LISTED_AS_STORED, 200)?.date).toBe('2026-10-16');
  });

  it('has nothing to say without a chain or without a target', () => {
    // Returning a stand-in here would be the fabricated `expiration_days: 0`
    // defect again, arrived at from the other direction.
    expect(nearestListedExpiry([], 30)).toBeNull();
    expect(nearestListedExpiry(LISTED_AS_STORED, 0)).toBeNull();
  });
});

describe('recorded examples', () => {
  it('never occupy the live-result fields', () => {
    // The hard rule: a recorded exchange must not be able to be read as a live
    // model answer. The store holds only the INDEX of the visible example, so
    // stepping it can touch nothing the live-result region renders.
    useAssistantStore.setState({
      params: null,
      clarification: null,
      refusal: null,
      error: null,
      gateDenied: null,
    });

    for (let i = 0; i < RECORDED_EXCHANGES.length + 2; i += 1) {
      useAssistantStore.getState().stepDemo(1, RECORDED_EXCHANGES.length);
      const st = useAssistantStore.getState();
      expect(st.params).toBeNull();
      expect(st.clarification).toBeNull();
      expect(st.refusal).toBeNull();
      expect(st.error).toBeNull();
      expect(st.gateDenied).toBeNull();
    }
  });

  it('wraps in both directions and stays a valid index', () => {
    const n = RECORDED_EXCHANGES.length;
    useAssistantStore.setState({ demoIndex: 0 });
    useAssistantStore.getState().stepDemo(-1, n);
    expect(useAssistantStore.getState().demoIndex).toBe(n - 1);
    useAssistantStore.getState().stepDemo(1, n);
    expect(useAssistantStore.getState().demoIndex).toBe(0);
  });

  it('every recorded exchange carries the provenance it claims', () => {
    // These are shown to a visitor as evidence of what the model does, so each
    // must name where and when it was captured. An entry without that is a
    // hand-written answer, which is the one thing this array may not hold.
    expect(RECORDED_EXCHANGES.length).toBeGreaterThan(0);
    for (const ex of RECORDED_EXCHANGES) {
      expect(ex.utterance.trim().length).toBeGreaterThan(0);
      expect(ex.capturedFrom).toBe('api.optionsandfuturescalculator.com');
      expect(ex.capturedAt).toMatch(/^\d{4}-\d{2}-\d{2}$/);
      expect(ex.params.symbol.trim().length).toBeGreaterThan(0);
      expect(ex.params.strategy.trim().length).toBeGreaterThan(0);
      // A zero expiry or a zero size is the fabricated-value signature; no
      // capture produced one, and one appearing here would mean the array was
      // edited by hand rather than transcribed.
      expect(ex.params.expirationDays).toBeGreaterThan(0);
      expect(ex.params.quantity).toBeGreaterThan(0);
    }
  });
});
