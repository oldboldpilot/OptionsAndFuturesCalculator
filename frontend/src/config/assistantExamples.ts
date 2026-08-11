/**
 * RECORDED assistant exchanges, for the demo strip in `AssistantPanel`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * ---------------------------------------------------------------------------
 * EVERY FIELD BELOW IS REAL CAPTURED OUTPUT. NONE OF IT IS WRITTEN BY HAND.
 *
 * Each entry was produced by POSTing its `utterance` to the live service on
 * `capturedAt`:
 *
 *   POST https://api.optionsandfuturescalculator.com
 *        /calculator.assistant.StrategyAssistant/ParseStrategy
 *   Content-Type: application/json      {"utterance": "..."}
 *
 * with a Pro credential, and transcribing the `params` object out of the
 * response verbatim. `exerciseType` and `asianType` are the wire values behind
 * the response's `"EUROPEAN"` / `"NOT_ASIAN"` — this service's zero values, and
 * the ones every capture returned.
 *
 * This matters more than it looks. The whole reason this file can exist at all
 * is that its contents are OBSERVATIONS, not plausible-looking prose: the same
 * rule that stops this codebase inventing a premium, a quote or a risk-free
 * rate applies to a model's answer. If a future example cannot be captured
 * from the running service, it does not go in this array — a hand-written
 * entry would be a fabricated model answer displayed to a visitor as evidence
 * of what the model does, which is the exact defect this project spends the
 * most effort avoiding, wearing friendlier clothes.
 *
 * They are also LABELLED as recorded at the point of display, every time, by
 * `AssistantPanel` — see the demo region there. Structurally, nothing in this
 * file can reach the live-result fields: `useAssistantStore` holds only the
 * index of the visible example, never its content, so a recorded parse cannot
 * occupy `params`, `clarification` or `refusal`.
 *
 * ---------------------------------------------------------------------------
 * ON THE CHOICE OF UTTERANCES.
 *
 * All three are equity option structures with an explicit expiry and contract
 * count, which is the shape the model is documented to handle well and is what
 * these captures show it doing. They are deliberately NOT chosen to hide the
 * model's gaps — the panel states those gaps permanently, immediately below,
 * whether or not an example is on screen. Two of the gaps were confirmed again
 * during this capture session: `QQQ` came back classified FUTURES and refused,
 * and an utterance with no stated expiry came back as a clarification. Neither
 * belongs in an advert, and neither is concealed by leaving it out of one.
 */
import type { ParsedStrategy } from '../store/useAssistantStore';

export interface RecordedExchange {
  /** Sent verbatim as `ParseRequest.utterance`. */
  utterance: string;
  /** The `StrategyParams` that came back, field for field. */
  params: ParsedStrategy;
  /** ISO-8601 date of the capture. Displayed; it is the claim's evidence. */
  capturedAt: string;
  /** The host that answered. Displayed for the same reason. */
  capturedFrom: string;
}

const HOST = 'api.optionsandfuturescalculator.com';

export const RECORDED_EXCHANGES: readonly RecordedExchange[] = [
  {
    utterance: 'bull call spread on NVDA, 30 days, 2 contracts',
    params: {
      symbol: 'NVDA',
      assetClass: 'EQUITY',
      strategy: 'bull_call_spread',
      expirationDays: 30,
      quantity: 2,
      farExpirationDays: 0,
      exerciseType: 0,
      asianType: 0,
    },
    capturedAt: '2026-08-11',
    capturedFrom: HOST,
  },
  {
    utterance: 'iron condor on SPY, 45 days, 1 contract',
    params: {
      symbol: 'SPY',
      assetClass: 'EQUITY',
      strategy: 'iron_condor',
      expirationDays: 45,
      quantity: 1,
      farExpirationDays: 0,
      exerciseType: 0,
      asianType: 0,
    },
    capturedAt: '2026-08-11',
    capturedFrom: HOST,
  },
  {
    utterance: 'long strangle on AMD, 14 days, 5 contracts',
    params: {
      symbol: 'AMD',
      assetClass: 'EQUITY',
      strategy: 'long_strangle',
      expirationDays: 14,
      quantity: 5,
      farExpirationDays: 0,
      exerciseType: 0,
      asianType: 0,
    },
    capturedAt: '2026-08-11',
    capturedFrom: HOST,
  },
];
