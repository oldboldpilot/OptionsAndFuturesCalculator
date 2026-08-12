'use client';

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Exercise style (European/American/Bermudan) and Asian averaging, priced
 * on `sensen.finance.Finance/PriceOptionTree` -- a trinomial tree, and a
 * DIFFERENT engine from the strategy panel's own Black-Scholes pricer next
 * door. Default state costs one segment row, one collapsed Asian row, one
 * collapsed Advanced row, and the comparison table -- most traders never
 * open the rest of this panel.
 */
import { useEffect } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import {
  useTreePricerStore,
  type AsianStyle,
  type ExerciseStyle,
  type TreePriceResult,
} from '../store/useTreePricerStore';
import { BermudanDateBuilder } from './BermudanDateBuilder';

const STYLE_LABEL: Record<ExerciseStyle, string> = {
  EUROPEAN: 'European',
  AMERICAN: 'American',
  BERMUDAN: 'Bermudan',
};

function greeksLine(result: TreePriceResult): React.ReactNode {
  if (!result.greeks) {
    return (
      <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
        &Delta; &mdash; &middot; &Gamma; &mdash; &middot; &Theta; &mdash;{' '}
        <span title="options.cppm returns no closed-form Greeks for an Asian option on this tree.">
          (Asian: no closed-form Greeks)
        </span>
      </span>
    );
  }
  return (
    <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
      &Delta; {result.greeks.delta.toFixed(3)} &middot; &Gamma; {result.greeks.gamma.toFixed(4)}{' '}
      &middot; &Theta; {result.greeks.theta.toFixed(3)}
    </span>
  );
}

export function ExerciseStylePanel() {
  const { ticket, spotPrice, riskFreeRate, chainExpirations, selectedExpiration } =
    useCalculatorStore();
  const {
    exerciseType,
    setExerciseType,
    droppedDateCount,
    asianType,
    setAsianType,
    averagingStates,
    setAveragingStates,
    steps,
    setSteps,
    advancedOpen,
    setAdvancedOpen,
    loading,
    error,
    notReady,
    gateDenied,
    results,
    priceTree,
  } = useTreePricerStore();

  const dte =
    chainExpirations.find((e) => e.date === (ticket.expiration || selectedExpiration))?.dte ?? 0;
  const yearsToExpiry = dte > 0 ? dte / 365 : 0;
  const isAsian = asianType !== 'NOT_ASIAN';

  // Re-price whenever an input this panel draws from changes. `priceTree`
  // itself debounces and discards stale responses, so this is safe to fire
  // on every dependency change including rapid ticket edits.
  useEffect(() => {
    priceTree();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [
    ticket.strike,
    ticket.impliedVolatility,
    ticket.expiration,
    ticket.optionType,
    spotPrice,
    riskFreeRate,
    selectedExpiration,
  ]);

  return (
    <div className="panel">
      <div className="panel-head">
        <span className="panel-title">Exercise &amp; Averaging</span>
        {loading && <span className="chip chip-live">pricing&hellip;</span>}
      </div>

      <div className="panel-body" style={{ display: 'flex', flexDirection: 'column', gap: '0.5rem' }}>
        {/* Exercise style -- a 3-way segment, the Buy/Sell idiom from
            OptionTicket.tsx widened to three states. */}
        <span className="stat-label">Exercise</span>
        <div className="segment" style={{ width: '100%' }}>
          {(['EUROPEAN', 'AMERICAN', 'BERMUDAN'] as ExerciseStyle[]).map((s) => (
            <button
              key={s}
              className="segment-item"
              style={{ flex: 1 }}
              data-active={exerciseType === s}
              onClick={() => setExerciseType(s)}
            >
              {STYLE_LABEL[s]}
            </button>
          ))}
        </div>

        {/* The tree prices without a dividend yield -- OptionTreeRequest has
            no dividend field, and the drift term is r - sigma^2/2. The
            strategy panel next door DOES price with q, so the two panels
            can silently price different instruments for the same symbol
            without this note. Kept right under the segment row -- this
            panel shares a flex column with OptionTicket/PositionLegs and a
            long Bermudan date list can push later content into an internal
            scroll, so the one thing every reading of this panel needs is
            placed where it is never at risk of that. */}
        <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
          Dividend yield is not modelled on this tree (drift is r &minus;
          &sigma;&sup2;/2). The strategy panel prices with a dividend yield;
          this panel does not.
        </span>

        {exerciseType === 'BERMUDAN' && (
          <>
            <BermudanDateBuilder yearsToExpiry={yearsToExpiry} />
            {droppedDateCount > 0 && (
              <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-warn)' }}>
                {droppedDateCount} date{droppedDateCount === 1 ? '' : 's'} dropped -- outside
                (0, years to expiry] or closer together than one tree step.
              </span>
            )}
          </>
        )}

        {/* Averaging -- a peer of the exercise segment above, not a disclosure
            row beneath it. Exercise style and averaging are two independent
            axes of the SAME contract, and hiding one behind a collapsed
            button made the app read as though it could not price an Asian at
            all: the feature shipped, reached production, and was reported
            missing by the person who asked for it. The default is Vanilla, so
            the row costs one segment and answers "is Asian here?" without a
            click. */}
        <div style={{ display: 'grid', gap: '0.1875rem' }}>
          <span className="stat-label">Averaging</span>
          <div className="segment" style={{ width: '100%' }}>
            {(
              [
                ['NOT_ASIAN', 'Vanilla'],
                ['AVERAGE_PRICE', 'Avg price'],
                ['AVERAGE_STRIKE', 'Avg strike'],
              ] as [AsianStyle, string][]
            ).map(([type, label]) => (
              <button
                key={type}
                className="segment-item"
                style={{ flex: 1 }}
                data-active={asianType === type}
                onClick={() => setAsianType(type)}
              >
                {label}
              </button>
            ))}
          </div>

          {isAsian && (
            <label style={{ display: 'grid', gap: '0.1875rem' }}>
              <span className="stat-label">Averaging states (10-200)</span>
              <input
                className="input num"
                type="number"
                min={10}
                max={200}
                step={1}
                value={averagingStates}
                onChange={(e) => setAveragingStates(Number(e.target.value))}
              />
            </label>
          )}

          {/* An Asian priced here is a SINGLE contract on the tree.
              This note used to say Asian legs could not be added to a strategy
              and that the payoff panels "still show the vanilla position".
              BOTH halves are now false: calculator.proto carries asian_type on
              Leg, the ticket can set it, and the engine refuses the whole
              response rather than drawing anything. Left as a correction
              rather than a deletion because the old sentence described the
              exact misreading -- a vanilla curve standing in for an Asian --
              that the refusal exists to prevent. Said here because the two
              panels sit in adjacent columns and otherwise look like one
              instrument. */}
          {isAsian && (
            <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
              Priced as a single contract on this tree. An Asian leg can be added to a
              strategy, but the payoff, P&amp;L and probability panels are drawn against the
              price at expiry and will say so rather than plot it.
            </span>
          )}
        </div>

        {/* Advanced -- steps only. lambda is deliberately never exposed:
            the kernel silently rewrites it when transition probabilities
            go negative (options.cppm:127-133), so a value set here could be
            silently ignored -- a control the engine may override is a lie. */}
        <div>
          <button
            className="btn"
            style={{ width: '100%', justifyContent: 'space-between' }}
            onClick={() => setAdvancedOpen(!advancedOpen)}
            aria-expanded={advancedOpen}
          >
            <span>Advanced</span>
            <span className="chip">{steps} steps</span>
          </button>

          {advancedOpen && (
            <label style={{ display: 'grid', gap: '0.1875rem', marginTop: '0.375rem' }}>
              <span className="stat-label">
                Tree steps{isAsian ? ` (capped 120 for Asian averaging)` : ''}
              </span>
              <input
                className="input num"
                type="number"
                min={2}
                max={isAsian ? 120 : 500}
                step={1}
                value={steps}
                onChange={(e) => setSteps(Number(e.target.value))}
              />
            </label>
          )}
        </div>

        {/* Results */}
        {gateDenied ? (
          <div className="empty-state empty-state--gate">
            <span className="empty-state-title">Needs Pro</span>
            <span>{gateDenied}</span>
          </div>
        ) : error ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Unavailable</span>
            <span>{error}</span>
          </div>
        ) : notReady ? (
          /* A precondition, in the NEUTRAL style -- deliberately not the
             --error branch above. "You have not picked a strike yet" is the
             next thing to do, not a failure, and rendering it as "Unavailable"
             in loss red tells a trader the calculator is broken when nothing
             is wrong. The title names the action for the same reason. */
          <div className="empty-state">
            <span className="empty-state-title">Not priced yet</span>
            <span>{notReady}</span>
          </div>
        ) : results.length === 0 ? (
          <div className="empty-state">
            <span className="empty-state-title">No result</span>
            <span>Pick a strike with a live quote to price the tree.</span>
          </div>
        ) : (
          <div>
            {results.map((r) => (
              <div className="stat" key={r.style} style={{ flexDirection: 'column', alignItems: 'stretch', gap: '0.125rem' }}>
                <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between' }}>
                  <span className="stat-label">{STYLE_LABEL[r.style]}</span>
                  <span className="stat-value" style={{ fontVariantNumeric: 'tabular-nums' }}>
                    {r.value.toFixed(4)}
                    {r.earlyExercisePremium !== null && (
                      <span
                        style={{
                          marginLeft: '0.375rem',
                          fontSize: 'var(--text-2xs)',
                          color:
                            r.earlyExercisePremium > 0
                              ? 'var(--color-profit)'
                              : 'var(--color-ink-400)',
                        }}
                        title="Value vs. the European price from this same batch"
                      >
                        {r.earlyExercisePremium > 0 ? '+' : ''}
                        {r.earlyExercisePremium.toFixed(4)}
                      </span>
                    )}
                  </span>
                </div>
                {greeksLine(r)}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

export default ExerciseStylePanel;
