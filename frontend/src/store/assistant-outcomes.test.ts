/**
 * `ParseStrategy` has three SUCCESSFUL outcomes and three failure routes, and
 * the store must keep all six apart.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `assistant.proto`'s banner is explicit that a clarifying question and a
 * refusal both return gRPC status OK, because encoding either as an error
 * would make every well-behaved exchange with a trader indistinguishable from
 * the service being down -- in gRPC metrics, in Envoy's access logs and in the
 * browser. So `clarification` and `refusal` are their own fields here, and
 * `error` stays null across both.
 *
 * The failure routes are discriminated by STATUS CODE and never by message
 * text. The assistant's Pro refusal is one sentence today and was reworded
 * twice in a single day on the calculator surface; a store matching on a
 * substring would fall through to the red "Unavailable" branch the moment
 * someone improved it, telling a would-be subscriber the product is broken
 * instead of offering it to them.
 *
 * BREAK DIRECTIONS, all four run and observed:
 *   - route PERMISSION_DENIED to `error` instead of `gateDenied` -> the gate
 *     tests fail, including the reworded-message one.
 *   - set `error` on a clarification -> the clarification tests fail.
 *   - read `getParams()` regardless of `getOutcomeCase()` -> the empty-oneof
 *     and refusal tests fail on a fabricated parse.
 *   - drop `prior_clarification` from the follow-up request -> the threading
 *     test fails.
 */
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  createFakeClient,
  settle,
  parseParams,
  parseClarification,
  parseRefusal,
  parseNothing,
  GRPC_PERMISSION_DENIED,
  GRPC_FAILED_PRECONDITION,
  REFUSAL_UNKNOWN_SYMBOL,
  REFUSAL_MODEL_UNAVAILABLE,
  type FakeParseResponse,
} from '../test/grpc-harness';

/** Read at call time so each test chooses the outcome of the next RPC. */
let nextOutcome: { ok: FakeParseResponse } | { fail: { code: number; message: string } } = {
  ok: parseNothing(),
};

const fake = createFakeClient({
  parseStrategy: () => nextOutcome,
});

vi.mock('../grpc/AssistantServiceClientPb', () => ({
  StrategyAssistantClient: class {
    parseStrategy = fake.client.parseStrategy;
  },
}));

// `useAssistantStore` imports `useCalculatorStore`, which constructs a Supabase
// client at module scope. Mocked so importing the store does not reach out.
vi.mock('../lib/supabase/client', () => ({
  createClient: () => ({
    auth: {
      getSession: async () => ({ data: { session: null }, error: null }),
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
    },
  }),
}));

vi.mock('../lib/licence', () => ({ authMetadata: () => ({}) }));

import { useAssistantStore } from './useAssistantStore';

function ask(utterance: string) {
  fake.calls.length = 0;
  useAssistantStore.setState({
    utterance,
    status: 'idle',
    params: null,
    clarification: null,
    refusal: null,
    gateDenied: null,
    modelLimit: null,
    error: null,
  });
}

/** The ParseRequest instances the store actually put on the wire. */
function sentRequests() {
  return fake.calls
    .filter((c) => c.method === 'parseStrategy')
    .map((c) => c.request as { getUtterance(): string; getPriorClarification(): string });
}

describe('assistant outcomes', () => {
  beforeEach(() => {
    useAssistantStore.getState().reset();
    nextOutcome = { ok: parseNothing() };
  });

  it('reads a params outcome field for field', async () => {
    ask('bull call spread on NVDA, 30 days, 2 contracts');
    nextOutcome = {
      ok: parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 30,
        quantity: 2,
      }),
    };
    await useAssistantStore.getState().parse();
    await settle(() => useAssistantStore.getState().status === 'idle', 'settled');

    const st = useAssistantStore.getState();
    expect(st.params).toEqual({
      symbol: 'NVDA',
      assetClass: 'EQUITY',
      strategy: 'bull_call_spread',
      expirationDays: 30,
      quantity: 2,
      farExpirationDays: 0,
      exerciseType: 0,
      asianType: 0,
    });
    expect(st.clarification).toBeNull();
    expect(st.refusal).toBeNull();
    expect(st.error).toBeNull();
    expect(st.gateDenied).toBeNull();
  });

  it('sends the utterance verbatim, with no prior clarification on a first turn', async () => {
    ask('  iron condor on SPY, 45 days, 1 contract  ');
    nextOutcome = {
      ok: parseParams({
        symbol: 'SPY',
        assetClass: 'EQUITY',
        strategy: 'iron_condor',
        expirationDays: 45,
        quantity: 1,
      }),
    };
    await useAssistantStore.getState().parse();

    const reqs = sentRequests();
    expect(reqs).toHaveLength(1);
    // Trimmed of the surrounding whitespace only. Anything more would be this
    // client normalizing what the model is meant to normalize, and the two
    // disagreeing about what the trader said.
    expect(reqs[0].getUtterance()).toBe('iron condor on SPY, 45 days, 1 contract');
    expect(reqs[0].getPriorClarification()).toBe('');
  });

  it('treats a clarification as a SUCCESS, not an error', async () => {
    ask('iron condor on SPY');
    nextOutcome = { ok: parseClarification('Which expiration did you have in mind?') };
    await useAssistantStore.getState().parse();
    await settle(() => useAssistantStore.getState().status === 'idle', 'settled');

    const st = useAssistantStore.getState();
    expect(st.clarification).toBe('Which expiration did you have in mind?');
    // The whole point. A question routed to `error` renders in loss red under
    // "Unavailable", which says the service is down while it is working.
    expect(st.error).toBeNull();
    expect(st.gateDenied).toBeNull();
    expect(st.modelLimit).toBeNull();
    expect(st.params).toBeNull();
    expect(st.refusal).toBeNull();
  });

  it('threads the prior question into the next request, then drops it', async () => {
    ask('iron condor on SPY');
    nextOutcome = { ok: parseClarification('Which expiration did you have in mind?') };
    await useAssistantStore.getState().parse();

    // The trader answers with a fragment that reads as nonsense alone.
    ask('45 days');
    nextOutcome = {
      ok: parseParams({
        symbol: 'SPY',
        assetClass: 'EQUITY',
        strategy: 'iron_condor',
        expirationDays: 45,
        quantity: 1,
      }),
    };
    await useAssistantStore.getState().parse();

    const reqs = sentRequests();
    expect(reqs).toHaveLength(1);
    expect(reqs[0].getUtterance()).toBe('45 days');
    expect(reqs[0].getPriorClarification()).toBe('Which expiration did you have in mind?');

    // The exchange is over, so a THIRD request must not still be answering the
    // finished conversation.
    ask('protective put on TSLA, 60 days, 1 contract');
    nextOutcome = {
      ok: parseParams({
        symbol: 'TSLA',
        assetClass: 'EQUITY',
        strategy: 'protective_put',
        expirationDays: 60,
        quantity: 1,
      }),
    };
    await useAssistantStore.getState().parse();
    expect(sentRequests()[0].getPriorClarification()).toBe('');
  });

  it('treats a refusal as a SUCCESS, keeping its machine-readable reason', async () => {
    ask('3-2-1 crack spread on crude');
    nextOutcome = {
      ok: parseRefusal(REFUSAL_UNKNOWN_SYMBOL, '"CND" does not resolve to a tradeable instrument.'),
    };
    await useAssistantStore.getState().parse();
    await settle(() => useAssistantStore.getState().status === 'idle', 'settled');

    const st = useAssistantStore.getState();
    expect(st.refusal).toEqual({
      reason: REFUSAL_UNKNOWN_SYMBOL,
      message: '"CND" does not resolve to a tradeable instrument.',
    });
    expect(st.error).toBeNull();
    // And emphatically no params. A refusal that also produced a parse would
    // put a fabricated symbol into the calculator behind an honest refusal.
    expect(st.params).toBeNull();
  });

  it('keeps MODEL_UNAVAILABLE a refusal, not an error', async () => {
    // The supported empty-MODEL_URL build answers exactly this. It is a
    // populated response with status OK by design, and the reason code is what
    // tells a caller to check the model rather than the market-data creds.
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = {
      ok: parseRefusal(REFUSAL_MODEL_UNAVAILABLE, 'The strategy assistant is not available in this deployment.'),
    };
    await useAssistantStore.getState().parse();

    const st = useAssistantStore.getState();
    expect(st.refusal?.reason).toBe(REFUSAL_MODEL_UNAVAILABLE);
    expect(st.error).toBeNull();
  });

  it('refuses to read an unset oneof as any of the three outcomes', async () => {
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = { ok: parseNothing() };
    await useAssistantStore.getState().parse();

    const st = useAssistantStore.getState();
    // Not a parse, not a question, not a refusal. A store that read
    // `getParams()` without checking the case would have set symbol "" and
    // expiry 0 here and rendered it as an answer.
    expect(st.params).toBeNull();
    expect(st.clarification).toBeNull();
    expect(st.refusal).toBeNull();
    expect(st.error).not.toBeNull();
  });

  it('routes PERMISSION_DENIED to the upgrade path, not the error path', async () => {
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = {
      fail: {
        code: GRPC_PERMISSION_DENIED,
        message:
          'The natural-language strategy assistant is a Pro feature. The calculator itself remains free -- build your strategy manually with the symbol and strategy selectors, or upgrade for assisted parsing.',
      },
    };
    await useAssistantStore.getState().parse();

    const st = useAssistantStore.getState();
    expect(st.gateDenied).toContain('Pro feature');
    // Critical: `error` must stay null, or the panel renders BOTH the upgrade
    // prompt and a red failure for the same refusal.
    expect(st.error).toBeNull();
    expect(st.modelLimit).toBeNull();
    expect(st.params).toBeNull();
  });

  it('keeps routing on the code when the gate copy is reworded', async () => {
    for (const message of [
      'The natural-language strategy assistant is a Pro feature.',
      'Assisted parsing needs a Pro subscription.',
      'Upgrade to describe trades in words.',
    ]) {
      ask('bull call spread on NVDA, 30 days');
      nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message } };
      await useAssistantStore.getState().parse();

      const st = useAssistantStore.getState();
      expect(st.gateDenied).toBe(message);
      expect(st.error).toBeNull();
    }
  });

  it('routes FAILED_PRECONDITION to a stated limitation, not to an outage', async () => {
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = { fail: { code: GRPC_FAILED_PRECONDITION, message: 'Outside what this model describes.' } };
    await useAssistantStore.getState().parse();

    const st = useAssistantStore.getState();
    expect(st.modelLimit).toBe('Outside what this model describes.');
    expect(st.error).toBeNull();
    expect(st.gateDenied).toBeNull();
  });

  it('routes every OTHER failure to the error path', async () => {
    // A dead backend is not a sales opportunity. Offering checkout when the
    // service is unreachable takes money for something that cannot run.
    for (const code of [2 /* UNKNOWN */, 3 /* INVALID_ARGUMENT */, 8 /* RESOURCE_EXHAUSTED */, 14 /* UNAVAILABLE */]) {
      ask('bull call spread on NVDA, 30 days');
      nextOutcome = { fail: { code, message: `failure with code ${code}` } };
      await useAssistantStore.getState().parse();

      const st = useAssistantStore.getState();
      expect(st.error).toBe(`failure with code ${code}`);
      expect(st.gateDenied).toBeNull();
      expect(st.modelLimit).toBeNull();
    }
  });

  it('clears the previous outcome before the next parse', async () => {
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = { fail: { code: GRPC_PERMISSION_DENIED, message: 'Needs Pro.' } };
    await useAssistantStore.getState().parse();
    expect(useAssistantStore.getState().gateDenied).not.toBeNull();

    // A licence was entered. The upgrade prompt must not outlive the refusal
    // that produced it, sitting above a correct answer.
    ask('bull call spread on NVDA, 30 days');
    nextOutcome = {
      ok: parseParams({
        symbol: 'NVDA',
        assetClass: 'EQUITY',
        strategy: 'bull_call_spread',
        expirationDays: 30,
        quantity: 2,
      }),
    };
    await useAssistantStore.getState().parse();

    const st = useAssistantStore.getState();
    expect(st.gateDenied).toBeNull();
    expect(st.params?.symbol).toBe('NVDA');
  });

  it('refuses an empty utterance without calling the service', async () => {
    ask('   ');
    await useAssistantStore.getState().parse();
    expect(sentRequests()).toHaveLength(0);
    expect(useAssistantStore.getState().error).not.toBeNull();
  });
});
