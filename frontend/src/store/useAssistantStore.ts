/**
 * The natural-language strategy assistant, client side.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `calculator.assistant.StrategyAssistant/ParseStrategy` turns a trader's own
 * sentence into the parameters the calculator already knows how to work with.
 * This store owns that one RPC and nothing else: it does not price, it does not
 * build legs, and it never invents a field the service did not send.
 *
 * ---------------------------------------------------------------------------
 * THREE OUTCOMES, ALL OF THEM SUCCESSFUL.
 *
 * `ParseResponse` is a oneof — params, a clarifying question, or a refusal —
 * and all three arrive with gRPC status OK. That is the contract's own choice
 * and its banner explains why: a question the model asks instead of guessing,
 * and a refusal it gives instead of fabricating a symbol, are the model doing
 * its job. Encoding either as a gRPC error would make every well-behaved
 * exchange indistinguishable from the service being down.
 *
 * So this store keeps `clarification` and `refusal` as their own fields, well
 * away from `error`. `error` means the RPC itself failed and retrying might
 * help; the other two mean the RPC worked and the answer is the answer.
 *
 * ---------------------------------------------------------------------------
 * AND THREE FAILURE ROUTES, DISCRIMINATED BY STATUS CODE.
 *
 * The same discipline `useCalculatorStore` follows, for the same reason: the
 * Pro gate's copy was reworded twice in a single day, so anything matching on
 * message text would have broken silently while still rendering something
 * plausible. `gateDenied` (7) renders as `UpgradePrompt`; `modelLimit` (9)
 * renders as a stated limitation; everything else is `error`, in loss red.
 *
 * These are this store's OWN fields rather than the calculator store's.
 * `useCalculatorStore.gateDenied` is documented as written only by
 * `calculateStrategy`, which clears it on entry — an assistant refusal parked
 * there would be wiped by the next calculation and would render the upgrade
 * prompt over the metrics panel, which is not the surface that was refused.
 */
import { create } from 'zustand';
import { StrategyAssistantClient } from '../grpc/AssistantServiceClientPb';
import { ParseRequest } from '../grpc/assistant_pb';
import { authMetadata } from '../lib/licence';
import { useCalculatorStore, type ChainExpiration } from './useCalculatorStore';

/**
 * `ParseResponse.outcome`'s field numbers, which are also the values of the
 * generated `ParseResponse.OutcomeCase`.
 *
 * Spelled out rather than imported for the same reason `RPC_PERMISSION_DENIED`
 * is in `useCalculatorStore`: importing the enum pulls the generated runtime
 * module into every consumer of this store, and these numbers are fixed by
 * `assistant.proto`'s oneof — changing one is a wire-breaking change that the
 * proto's own comment forbids doing casually.
 *
 * `OUTCOME_NOT_SET` (0) is deliberately handled rather than folded into one of
 * the others. A response with nothing set is not a refusal and not a question;
 * it is a service that answered without answering, and quietly reading it as
 * "no params" would present that as the model declining.
 */
const OUTCOME_NOT_SET = 0;
const OUTCOME_PARAMS = 1;
const OUTCOME_CLARIFICATION = 2;
const OUTCOME_REFUSAL = 3;

/** gRPC `PERMISSION_DENIED`. Fixed by the gRPC specification, not by a library. */
const RPC_PERMISSION_DENIED = 7;

/**
 * gRPC `FAILED_PRECONDITION`.
 *
 * No path in `assistant_service.cpp` returns this today — the assistant's own
 * "I cannot do that" answers are Refusals with status OK, by design. It is
 * wired anyway, and deliberately: `useCalculatorStore` already routes 9 to a
 * stated limitation rather than to the red "Unavailable" branch, and a client
 * that dropped the distinction here would present the first such limitation
 * this service ever grows as an outage.
 */
const RPC_FAILED_PRECONDITION = 9;

/** `Refusal.Reason`, by number. The proto's enum; the words are this UI's. */
export const REFUSAL_REASON_LABEL: Record<number, string> = {
  0: 'Unspecified',
  1: 'Unsupported strategy',
  2: 'Unknown symbol',
  3: 'Out of scope',
  4: 'Model unavailable',
  5: 'Market data unavailable',
};

/** The asset classes `setSymbol` accepts. `asset_class` is a free string on the wire. */
const ASSET_CLASSES = ['EQUITY', 'FUTURES', 'CRYPTO'] as const;
export type AssetClass = (typeof ASSET_CLASSES)[number];

/**
 * A parse, field for field as `StrategyParams` sent it.
 *
 * Nothing is normalised, defaulted or widened on the way in. `assetClass` in
 * particular stays the raw string: it is validated at APPLY time, where a value
 * outside the three the calculator knows can be refused by name, rather than
 * being coerced here into whichever of them looks closest.
 */
export interface ParsedStrategy {
  symbol: string;
  assetClass: string;
  /** A catalogue id — `bull_call_spread`, not "Bull Call Spread". */
  strategy: string;
  expirationDays: number;
  quantity: number;
  /** 0 means "not stated", which is the common case and not an error. */
  farExpirationDays: number;
  /** `sensen.finance.ExerciseType`. 0 is EUROPEAN. */
  exerciseType: number;
  /** `sensen.finance.AsianType`. 0 is NOT_ASIAN. */
  asianType: number;
}

export interface ParsedRefusal {
  reason: number;
  message: string;
}

export type AssistantStatus = 'idle' | 'parsing';

/**
 * Nearest LISTED expiry to the one the trader asked for.
 *
 * The model reports calendar days as stated ("thirty days out"); the chain
 * lists actual expiration dates, and there is very rarely one exactly that far
 * away. Snapping to the nearest is the honest move — but only if the UI then
 * says which date it snapped to and how far that is from the request, which is
 * what `applyNote` carries. Silently pricing 44 days as though it were the 45
 * the trader said is the same class of substitution as a fabricated premium.
 *
 * Exported for its own test: it is the one piece of arithmetic here, and a
 * "nearest" that quietly returned the first element would look right on any
 * chain whose first expiry happened to be closest.
 */
export function nearestListedExpiry(
  expirations: ChainExpiration[],
  targetDays: number,
): ChainExpiration | null {
  if (expirations.length === 0 || targetDays <= 0) return null;
  return expirations.reduce((best, e) =>
    Math.abs(e.dte - targetDays) < Math.abs(best.dte - targetDays) ? e : best,
  );
}

interface AssistantState {
  /** What the trader typed. Sent verbatim; the model does its own normalising. */
  utterance: string;
  /**
   * The single question this service asked last turn, threaded back on the
   * next request so an answer like "next Friday" is not read in isolation.
   * Exactly one prior question, which is all `ParseRequest` carries and all
   * this RPC's own contract ever produces.
   */
  priorClarification: string;
  status: AssistantStatus;

  params: ParsedStrategy | null;
  clarification: string | null;
  refusal: ParsedRefusal | null;

  /** PERMISSION_DENIED. Renders as `UpgradePrompt`, never as an error. */
  gateDenied: string | null;
  /** FAILED_PRECONDITION. A stated limitation, not a failure. */
  modelLimit: string | null;
  /** The RPC itself failed. Retrying might help. */
  error: string | null;

  /**
   * The catalogue id a successful apply selected. `StrategySelector` mirrors
   * this into its own selection so the parse lands in the picker the user
   * already knows, rather than in a second, parallel way to build a position.
   */
  selectedStrategyId: string | null;
  /**
   * Bumped on every successful apply.
   *
   * `selectedStrategyId` alone is not enough to drive the picker: applying the
   * SAME parse twice does not change it, so a user who applied, then clicked a
   * different strategy by hand, then pressed Apply again would watch the
   * button do nothing. The counter makes each apply a distinct event.
   */
  applySeq: number;
  /**
   * The expiry an apply asked for and has not yet resolved against a chain.
   * Non-zero means `resolvePendingExpiry` still has work to do once the chain
   * for the new symbol arrives.
   */
  pendingExpirationDays: number;
  /** What applying actually did, in the user's terms. Includes the expiry snap. */
  applyNote: string | null;
  /** Why applying was refused. Set instead of applying, never alongside it. */
  applyBlocked: string | null;

  /**
   * Which RECORDED example the demo strip is showing.
   *
   * An INDEX, and deliberately nothing more. The recorded exchanges live in
   * `config/assistantExamples.ts` and are read straight from there by the
   * component that renders them, so no part of a recorded parse ever occupies
   * `params`, `clarification` or `refusal`. That is not tidiness — those three
   * fields are what the live-result region renders, and the one thing a demo
   * must never be able to do is appear there. Keeping the store's knowledge of
   * the demo down to a number makes "a recorded answer cannot be mistaken for
   * a live one" a property of the shape of the state rather than of anybody
   * remembering to label it, which the UI does as well.
   */
  demoIndex: number;

  setUtterance: (v: string) => void;
  /** Step the recorded-example strip. Wraps. Touches no outcome field. */
  stepDemo: (delta: number, count: number) => void;
  parse: () => Promise<void>;
  applyParams: (known: { ids: string[]; multiExpiry: string[] }) => void;
  resolvePendingExpiry: () => void;
  reset: () => void;
}

const CLEAR_OUTCOMES = {
  params: null,
  clarification: null,
  refusal: null,
  gateDenied: null,
  modelLimit: null,
  error: null,
  applyNote: null,
  applyBlocked: null,
} as const;

export const useAssistantStore = create<AssistantState>((set, get) => ({
  utterance: '',
  priorClarification: '',
  status: 'idle',
  ...CLEAR_OUTCOMES,
  selectedStrategyId: null,
  applySeq: 0,
  pendingExpirationDays: 0,

  demoIndex: 0,

  setUtterance: (v) => set({ utterance: v }),

  // Writes `demoIndex` and NOTHING else. A recorded example that also cleared
  // -- or worse, populated -- an outcome field would be a hand-recorded answer
  // sitting in the place the live one renders.
  stepDemo: (delta, count) =>
    set((st) => ({
      demoIndex: count > 0 ? (((st.demoIndex + delta) % count) + count) % count : 0,
    })),

  reset: () =>
    set({
      utterance: '',
      priorClarification: '',
      status: 'idle',
      ...CLEAR_OUTCOMES,
      selectedStrategyId: null,
      applySeq: 0,
      pendingExpirationDays: 0,
    }),

  parse: async () => {
    const utterance = get().utterance.trim();
    if (utterance === '') {
      set({ ...CLEAR_OUTCOMES, error: 'Describe the trade before parsing it.' });
      return;
    }

    // Every prior outcome clears HERE, above the call, rather than in each
    // branch below -- the same placement `calculateStrategy` uses and for the
    // same reason. A refusal belongs to the utterance that provoked it, and
    // leaving one on screen beside a fresh answer attributes it to the wrong
    // sentence.
    set({ status: 'parsing', ...CLEAR_OUTCOMES });

    try {
      const backendUrl =
        process.env.NEXT_PUBLIC_API_URL || 'https://api.optionsandfuturescalculator.com';
      const client = new StrategyAssistantClient(backendUrl);
      const req = new ParseRequest();
      req.setUtterance(utterance);
      const prior = get().priorClarification;
      if (prior) req.setPriorClarification(prior);

      const res = await client.parseStrategy(req, authMetadata());

      // Widened to `number` on purpose: the switch below compares against the
      // spelled-out field numbers, and comparing a generated enum member type
      // to a numeric literal is a type error under `strict`.
      const outcome: number = res.getOutcomeCase();

      if (outcome === OUTCOME_PARAMS) {
        const p = res.getParams();
        if (!p) {
          // The oneof said PARAMS and the accessor gave nothing. Impossible
          // through the generated deserializer, and still not something to
          // paper over with an empty ParsedStrategy full of zeros -- a symbol
          // of "" and an expiry of 0 would read as a parse.
          set({ status: 'idle', error: 'The assistant reported parameters it did not send.' });
          return;
        }
        set({
          status: 'idle',
          // A completed parse ends the clarification exchange. Threading the
          // stale question into the NEXT, unrelated request would have the
          // model answering a conversation that finished.
          priorClarification: '',
          params: {
            symbol: p.getSymbol(),
            assetClass: p.getAssetClass(),
            strategy: p.getStrategy(),
            expirationDays: p.getExpirationDays(),
            quantity: p.getQuantity(),
            farExpirationDays: p.getFarExpirationDays(),
            exerciseType: p.getExerciseType(),
            asianType: p.getAsianType(),
          },
        });
        return;
      }

      if (outcome === OUTCOME_CLARIFICATION) {
        const question = res.getClarification()?.getQuestion() ?? '';
        set({
          status: 'idle',
          clarification: question,
          // Threaded into the next request, which is the whole point of the
          // field: without it the trader's "the second one" reaches the model
          // as a fragment that reads as nonsense.
          priorClarification: question,
        });
        return;
      }

      if (outcome === OUTCOME_REFUSAL) {
        const r = res.getRefusal();
        set({
          status: 'idle',
          priorClarification: '',
          refusal: { reason: r?.getReason() ?? 0, message: r?.getMessage() ?? '' },
        });
        return;
      }

      // OUTCOME_NOT_SET, or a branch added to the proto that this client
      // predates. Either way it is not a parse and must not be shown as one.
      set({
        status: 'idle',
        error:
          outcome === OUTCOME_NOT_SET
            ? 'The assistant returned an empty response.'
            : `The assistant returned an outcome this build does not understand (${outcome}).`,
      });
    } catch (err: unknown) {
      const message = (err as Error).message || 'The strategy assistant is unreachable.';
      const code = (err as { code?: number }).code;

      // Matched on the CODE, never the message. `check_assistant_entitlement`
      // gates every call to both assistants, and its sentence is copy.
      if (code === RPC_PERMISSION_DENIED) {
        set({ status: 'idle', gateDenied: message });
        return;
      }
      if (code === RPC_FAILED_PRECONDITION) {
        set({ status: 'idle', modelLimit: message });
        return;
      }
      set({ status: 'idle', error: message });
    }
  },

  /**
   * Push a parse into the calculator.
   *
   * What this sets: the symbol (and with it the asset class, which drives the
   * quote and chain fetch), the strategy selected in the picker, the ticket's
   * contract count, and — once the chain arrives — the nearest listed expiry.
   *
   * What it deliberately does NOT set: legs, strikes or premiums. Those come
   * off the live chain when the user presses Apply in the picker, exactly as
   * they do for a strategy chosen by hand. `StrategyParams` carries no strike
   * and no premium, so anything this function put there would be invented, and
   * an invented premium is the single substitution that makes every payoff
   * figure downstream wrong.
   *
   * `known` is passed in rather than imported so the catalogue stays owned by
   * `StrategySelector`, which renders it. A second copy of that list here is
   * exactly how the picker and the assistant would come to disagree about
   * which structures exist.
   */
  applyParams: (known) => {
    const p = get().params;
    if (!p) return;

    if (!(ASSET_CLASSES as readonly string[]).includes(p.assetClass)) {
      // Refuse rather than classify. Picking the nearest-looking class here
      // would set the calculator to price one instrument against another's
      // quote -- the ES / Eversource collision, arrived at from the client.
      set({
        applyBlocked: `The parse names asset class "${p.assetClass}", which this calculator does not price.`,
      });
      return;
    }

    if (!known.ids.includes(p.strategy)) {
      // Either a catalogue id this build does not carry, or one the picker
      // hides. Both mean the same thing to the user: the assistant named a
      // structure they cannot select, and saying so beats selecting nothing.
      set({
        applyBlocked: `The parse names strategy "${p.strategy}", which this build's picker does not offer.`,
      });
      return;
    }

    const calc = useCalculatorStore.getState();
    // Spot is left to the quote service: no third argument means "fetch it",
    // and a user-supplied simulation price is the only thing that overrides.
    calc.setSymbol(p.symbol, undefined, p.assetClass as AssetClass);
    calc.setTicket({ quantity: p.quantity > 0 ? p.quantity : 1 });

    const notes: string[] = [];
    if (p.quantity <= 0) notes.push('No contract count was stated; the ticket keeps 1.');
    if (known.multiExpiry.includes(p.strategy)) {
      notes.push(
        p.farExpirationDays > 0
          ? `This structure needs two expiries (${p.expirationDays}d near, ${p.farExpirationDays}d far). The single-chain builder applies one — add the far leg from the second chain.`
          : 'This structure needs two expiries and only the near one was stated. Add the far leg from a second chain.',
      );
    }

    set({
      selectedStrategyId: p.strategy,
      applySeq: get().applySeq + 1,
      pendingExpirationDays: p.expirationDays,
      applyBlocked: null,
      applyNote: notes.length > 0 ? notes.join(' ') : null,
    });

    // The chain for the OLD symbol may still be loaded, and if the symbol did
    // not change it is the right one already. Try immediately; the component
    // effect retries when a new chain lands.
    get().resolvePendingExpiry();
  },

  /**
   * Snap the pending expiry onto a listed one, once there is a chain to snap
   * against. Idempotent: it clears `pendingExpirationDays` on the way out, so
   * the effect that drives it cannot loop against the reload
   * `setSelectedExpiration` triggers.
   */
  resolvePendingExpiry: () => {
    const target = get().pendingExpirationDays;
    if (target <= 0) return;

    const calc = useCalculatorStore.getState();
    const match = nearestListedExpiry(calc.chainExpirations, target);

    if (!match) {
      // No option expirations. For a futures symbol that is the normal case --
      // the term structure carries delivery months instead - so wait until the
      // chain has actually finished before saying anything.
      if (calc.chainStatus === 'loading' || calc.chainStatus === 'idle') return;
      set({
        pendingExpirationDays: 0,
        applyNote: [
          get().applyNote,
          calc.futuresCurve.length > 0
            ? `No option chain for ${calc.symbol}; a futures leg takes its expiry from the delivery month on the term structure, not from the ${target}d stated.`
            : `No listed expirations for ${calc.symbol}, so the ${target}d stated could not be matched to a real contract.`,
        ]
          .filter(Boolean)
          .join(' '),
      });
      return;
    }

    calc.setSelectedExpiration(match.date);
    set({
      pendingExpirationDays: 0,
      applyNote: [
        get().applyNote,
        // Always stated, even on an exact hit: the trader said a number of
        // days and the position will be priced to a DATE, and which date that
        // is has to be visible without opening the ticket.
        match.dte === target
          ? `Expiry ${match.date} (${match.dte}d), exactly as stated.`
          : `Nearest listed expiry to ${target}d is ${match.date} (${match.dte}d).`,
      ]
        .filter(Boolean)
        .join(' '),
    });
  },
}));
