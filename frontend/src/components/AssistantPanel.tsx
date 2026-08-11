'use client';

import React, { useEffect } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import {
  useAssistantStore,
  REFUSAL_REASON_LABEL,
  type ParsedStrategy,
} from '../store/useAssistantStore';
import { RECORDED_EXCHANGES } from '../config/assistantExamples';
import {
  SELECTABLE_STRATEGY_IDS,
  MULTI_EXPIRY_STRATEGY_IDS,
  strategyDisplayName,
} from './StrategySelector';
import { useProStatus } from '../lib/useProStatus';
import UpgradePrompt from './UpgradePrompt';

/**
 * Describe a trade in words; the picker below fills in.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This is a COMPOSER, not a chat. There is no transcript, no bubbles and no
 * persona, because the service behind it is a single-turn parser with one
 * optional follow-up question -- `ParseRequest` carries the trader's utterance
 * and at most one prior clarification, and that is the whole of its state.
 * Rendering it as a conversation would advertise a memory it does not have.
 *
 * It also does not apply anything by itself. A parse names a symbol, a
 * structure, an expiry and a contract count; it carries no strike and no
 * premium, and those are the two numbers that decide every payoff figure
 * downstream. So Apply sets the symbol, selects the structure in the picker,
 * snaps to a listed expiry and sets the contract count -- and then stops,
 * leaving the ordinary Apply button to build the legs from the live chain.
 *
 * ---------------------------------------------------------------------------
 * THE RECORDED-EXAMPLE STRIP.
 *
 * Assisted parsing is Pro-only and stays that way -- there is no free
 * allowance here and no trial count. What a visitor who has not paid gets
 * instead is EVIDENCE: real exchanges captured from the running service, so
 * the decision to subscribe is made after seeing the thing rather than before.
 * The live box still submits for real, and an unentitled caller gets the
 * engine's own refusal through `UpgradePrompt` a moment after they have seen
 * what they are being refused.
 *
 * Three things keep that demo from being read as a live answer, and all three
 * are load-bearing:
 *
 *   1. Its content comes from `config/assistantExamples.ts` and is real
 *      captured output, never hand-written prose that looks like output.
 *   2. It cannot reach the live-result fields. `useAssistantStore` holds only
 *      the INDEX of the visible example, so a recorded parse structurally
 *      cannot occupy `params`, `clarification` or `refusal`.
 *   3. It is labelled at the point of display, every time, with the capture
 *      date and the host that answered -- not in a footnote, and not only on
 *      first render.
 *
 * The parameter chips themselves are the SAME component in both regions, on
 * purpose: an example that rendered differently from a real parse would be
 * advertising a product the user is not going to get.
 *
 * ---------------------------------------------------------------------------
 * The limits below the box are permanent copy, not an error state. Each one is
 * a place where this model is known to be wrong or silent, and a trader who
 * types into one of those gaps gets the service's own refusal rather than a
 * plausible-looking strategy they never asked for.
 */

/** `sensen.finance.ExerciseType`, by wire value. */
const EXERCISE_LABEL: Record<number, string> = {
  0: 'European',
  1: 'American',
  2: 'Bermudan',
};

/** `sensen.finance.AsianType`, by wire value. */
const ASIAN_LABEL: Record<number, string> = {
  0: 'Vanilla',
  1: 'Avg price',
  2: 'Avg strike',
};

const noteStyle: React.CSSProperties = {
  fontSize: 'var(--text-2xs)',
  color: 'var(--color-ink-400)',
  lineHeight: 1.35,
};

/**
 * A parse, as chips. Shared by the live result and the recorded example so the
 * two cannot drift into showing different things -- which would make the demo
 * an advert for a rendering the product does not have.
 */
function ParsedChips({ p }: { p: ParsedStrategy }) {
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.25rem' }}>
      <span className="chip chip-accent">{p.symbol}</span>
      <span className="chip">{p.assetClass}</span>
      <span className="chip">{strategyDisplayName(p.strategy)}</span>
      <span className="chip">{p.expirationDays}d</span>
      {p.farExpirationDays > 0 && (
        <span className="chip" title="Far leg expiry">far {p.farExpirationDays}d</span>
      )}
      <span className="chip">{p.quantity}×</span>
    </div>
  );
}

export function AssistantPanel() {
  const {
    utterance, setUtterance, status, parse,
    params, clarification, refusal,
    gateDenied, modelLimit, error,
    applyParams, applyNote, applyBlocked, resolvePendingExpiry,
    pendingExpirationDays, priorClarification,
    demoIndex, stepDemo,
  } = useAssistantStore();

  const { pro } = useProStatus();

  // The expiry snap needs a chain, and the chain arrives after `setSymbol`
  // fires. Re-run whenever either the listed expirations or the chain's status
  // moves; `resolvePendingExpiry` clears the pending days on its way out, so
  // the reload that `setSelectedExpiration` triggers cannot loop back here.
  const chainExpirations = useCalculatorStore((s) => s.chainExpirations);
  const chainStatus = useCalculatorStore((s) => s.chainStatus);
  useEffect(() => {
    if (pendingExpirationDays > 0) resolvePendingExpiry();
  }, [chainExpirations, chainStatus, pendingExpirationDays, resolvePendingExpiry]);

  const busy = status === 'parsing';
  const demoCount = RECORDED_EXCHANGES.length;
  // Clamped rather than trusted: the index is persisted store state and the
  // array is edited by hand, so a shortened array must not index past its end.
  const demo = demoCount > 0 ? RECORDED_EXCHANGES[demoIndex % demoCount] : null;

  function submit() {
    if (!busy) void parse();
  }

  return (
    <div className="panel" style={{ flex: 'none' }}>
      <div className="panel-head">
        <span className="panel-title">Assistant</span>
        {/* Marked, not disabled -- the same rule the strategy list follows.
            The gate is the engine's and it explains itself in its own words;
            blocking the button here would replace a specific server refusal
            with a vaguer client-side one. */}
        {!pro && (
          <span className="chip chip-accent" title="Assisted parsing needs Pro">PRO</span>
        )}
      </div>

      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', gap: '0.4375rem' }}>
        <span style={noteStyle}>
          Type a trade in plain English. A fine-tuned model returns the symbol, structure,
          expiry and size — never a strike or a price.
        </span>

        <textarea
          className="input"
          rows={2}
          value={utterance}
          placeholder={'bull call spread on NVDA, 30 days, 2 contracts'}
          aria-label="Describe the trade in plain English"
          onChange={(e) => setUtterance(e.target.value)}
          onKeyDown={(e) => {
            // Enter submits; Shift+Enter is a newline. A parse is one short
            // sentence, so the common case should not need the mouse.
            if (e.key === 'Enter' && !e.shiftKey) {
              e.preventDefault();
              submit();
            }
          }}
          style={{ resize: 'vertical', fontFamily: 'var(--font-mono)' }}
        />

        {/* The question this service asked last turn travels with the next
            request. Say so, or an answer typed as a fragment looks like it is
            being sent into the void. */}
        {priorClarification !== '' && (
          <span style={noteStyle}>
            Your next parse is sent as an answer to: “{priorClarification}”
          </span>
        )}

        <button
          className="btn btn-primary"
          onClick={submit}
          disabled={busy || utterance.trim() === ''}
        >
          {busy ? 'Parsing…' : 'Parse'}
        </button>

        {/* ------------------------------------------------------------------
            Outcomes. Exactly one of these regions renders at a time, and the
            three OK outcomes are visually distinct from the two failures --
            a question and a refusal are the model working, not the service
            being down. */}

        {gateDenied && <UpgradePrompt reason={gateDenied} />}

        {modelLimit && (
          <div className="empty-state">
            <span className="empty-state-title">Not covered</span>
            <span>{modelLimit}</span>
          </div>
        )}

        {error && (
          <div className="empty-state">
            <span className="empty-state-title" style={{ color: 'var(--color-loss)' }}>
              Unavailable
            </span>
            <span>{error}</span>
          </div>
        )}

        {clarification && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.25rem' }}>
            <span className="chip">Question</span>
            <span style={{ fontSize: 'var(--text-xs)', color: 'var(--color-ink-200)' }}>
              {clarification}
            </span>
            <span style={noteStyle}>
              The assistant is missing one fact it will not guess. Answer above and parse again.
            </span>
          </div>
        )}

        {refusal && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.25rem' }}>
            <span className="chip">
              {REFUSAL_REASON_LABEL[refusal.reason] ?? `Reason ${refusal.reason}`}
            </span>
            {/* The service's own sentence, verbatim. It knows what it refused
                and why more precisely than this component could guess, and a
                paraphrase here would drift from the refusal that was made. */}
            <span style={{ fontSize: 'var(--text-xs)', color: 'var(--color-ink-200)' }}>
              {refusal.message}
            </span>
            <span style={noteStyle}>
              Declining is a successful answer, not a failure — the alternative would be a
              structure or a symbol you did not ask for. Build it by hand in the picker below.
            </span>
          </div>
        )}

        {params && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.375rem' }}>
            <ParsedChips p={params} />

            {/* Neither of these came from the model. Saying so is the point:
                the training set holds zero instances of either concept, so
                they are produced by a whole-word keyword scan over the
                sentence and are only as good as that scan. They are shown and
                deliberately NOT applied -- the picker's Apply builds legs from
                the chain and never reads them, so writing them into the ticket
                would set a style on a leg the position does not use. */}
            {(params.exerciseType !== 0 || params.asianType !== 0) && (
              <span style={noteStyle}>
                Read from your wording by keyword, not by the model:{' '}
                {EXERCISE_LABEL[params.exerciseType] ?? params.exerciseType} exercise,{' '}
                {ASIAN_LABEL[params.asianType] ?? params.asianType}. Not applied — set it
                yourself in the Ticket and the Exercise &amp; Averaging panel.
              </span>
            )}

            <button
              className="btn"
              onClick={() =>
                applyParams({
                  ids: SELECTABLE_STRATEGY_IDS,
                  multiExpiry: MULTI_EXPIRY_STRATEGY_IDS,
                })
              }
            >
              Apply symbol, structure and expiry
            </button>

            <span style={noteStyle}>
              Strikes and premiums are not part of a parse. They come off the live chain when
              you press Apply in the picker below.
            </span>

            {applyBlocked && (
              <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-loss)' }}>
                {applyBlocked}
              </span>
            )}
            {applyNote && <span style={noteStyle}>{applyNote}</span>}
          </div>
        )}

        {/* ------------------------------------------------------------------
            Recorded examples. Never a live answer, and said so here rather
            than anywhere the user might not be looking. The dashed frame is
            the visual half of the same statement -- nothing else in this app
            is drawn that way -- but the WORDS are what carry it, because a
            border is not a claim. */}
        {demo && (
          <div
            style={{
              border: '1px dashed var(--color-line-strong)',
              padding: '0.375rem 0.4375rem',
              display: 'flex',
              flexDirection: 'column',
              gap: '0.3125rem',
              background: 'var(--color-base-700)',
            }}
          >
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.3125rem' }}>
              <span
                className="chip"
                style={{ borderStyle: 'dashed', letterSpacing: '0.06em', fontWeight: 700 }}
              >
                RECORDED EXAMPLE
              </span>
              <span style={{ ...noteStyle, marginLeft: 'auto' }}>
                {demoIndex + 1}/{demoCount}
              </span>
              <button
                className="btn"
                style={{ padding: '0 0.3125rem' }}
                aria-label="Previous recorded example"
                onClick={() => stepDemo(-1, demoCount)}
              >
                ‹
              </button>
              <button
                className="btn"
                style={{ padding: '0 0.3125rem' }}
                aria-label="Next recorded example"
                onClick={() => stepDemo(1, demoCount)}
              >
                ›
              </button>
            </div>

            <span style={{ fontSize: 'var(--text-2xs)', fontFamily: 'var(--font-mono)', color: 'var(--color-ink-200)' }}>
              “{demo.utterance}”
            </span>

            <ParsedChips p={demo.params} />

            <span style={noteStyle}>
              Not a live answer. Captured from {demo.capturedFrom} on {demo.capturedAt} and
              replayed here — the assistant did not run just now. Type your own above and
              press Parse for a real one.
            </span>
          </div>
        )}

        {/* ------------------------------------------------------------------
            Known gaps. Permanently visible, because each is a case where the
            answer is silence or a refusal, and a user who does not know that
            reads either as the site being broken. */}
        <div
          style={{
            ...noteStyle,
            display: 'flex',
            flexDirection: 'column',
            gap: '0.125rem',
            borderTop: '1px solid var(--color-line-soft)',
            paddingTop: '0.375rem',
          }}
        >
          <span>Trained on ES and NQ futures only — commodity roots are refused, not guessed.</span>
          <span>A bare futures directive (“Long NQ, 45 days”) often returns no parameters.</span>
          <span>Exercise style and averaging come from a keyword scan of your words.</span>
        </div>
      </div>
    </div>
  );
}

export default AssistantPanel;
