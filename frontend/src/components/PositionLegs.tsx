'use client';

import { useCalculatorStore, type Leg } from '../store/useCalculatorStore';
import { formatAmount, useCurrency } from '../lib/currency';
import { MoneyInput } from './MoneyInput';

/** The leg's own expiry, stated -- not inferred from the ticket above it. */
function expiryText(leg: Leg): string {
  const days = leg.expiration_days;
  if (leg.expiry_label && days !== undefined) return `${leg.expiry_label} · ${days}d`;
  if (leg.expiry_label) return leg.expiry_label;
  if (days !== undefined) return `${days}d`;
  return '';
}

/**
 * Editable leg list.
 *
 * Strike, quantity and premium are all live-editable, matching
 * optionsprofitcalculator.com's behaviour of letting the trader override any
 * fill price.
 *
 * One CARD per leg rather than one table row. The table this replaced listed
 * side, type, strike, quantity, premium, delta, theta, open and a remove
 * button -- about 440px of columns in a panel with 258px of body -- so the
 * quantity box was squeezed to 6px of content (the digit hidden under the
 * browser's spinner), and the remove button sat 118px outside the panel behind
 * a sideways scroll nobody had a reason to attempt. A leg is a small record:
 * what it is on the first line, what you can change on the second.
 */
export function PositionLegs() {
  // Regroup the strike column when the display locale changes.
  useCurrency();
  const { legs, symbol, updateLeg, removeLeg, clearLegs, calculateStrategy, result } =
    useCalculatorStore();

  return (
    <div className="panel">
      <div className="panel-head">
        <h2 className="panel-title">
          Position · {symbol} · {legs.length} leg{legs.length === 1 ? '' : 's'}
        </h2>
        {legs.length > 0 && (
          <button className="btn" onClick={clearLegs}>
            Clear
          </button>
        )}
      </div>

      <div className="panel-body panel-body--flush">
        {legs.length === 0 ? (
          <div className="empty-state">
            <span className="empty-state-title">No legs</span>
            <span>Select strikes from the option chain to build a position.</span>
          </div>
        ) : (
          <ul className="legs">
            {legs.map((leg, i) => {
              const risk = result?.legRisk.find((r) => r.legIndex === i) ?? null;
              const isOption = leg.option_type === 'CALL' || leg.option_type === 'PUT';
              const side = leg.action === 'BUY' ? 'Buy' : 'Sell';
              return (
                <li key={leg.id} className="leg" data-testid="leg-row">
                  <div className="leg-head">
                    {/* This is a toggle and it now says so. It looked like a
                        status label and flipped the whole leg when pressed. */}
                    <button
                      className={leg.action === 'BUY' ? 'btn btn-buy' : 'btn btn-sell'}
                      style={{ padding: '0.0625rem 0.5rem' }}
                      data-field="side"
                      aria-label={`${side} leg. Press to switch to ${leg.action === 'BUY' ? 'sell' : 'buy'}.`}
                      title="Switch between buy and sell"
                      onClick={() => {
                        updateLeg(leg.id, { action: leg.action === 'BUY' ? 'SELL' : 'BUY' });
                        calculateStrategy();
                      }}
                    >
                      {leg.action}
                    </button>
                    <span
                      className="leg-name"
                      style={{
                        color: leg.option_type === 'CALL' ? 'var(--color-profit-text)'
                          : leg.option_type === 'PUT' ? 'var(--color-loss)'
                          : 'var(--color-ink-100)',
                      }}
                    >
                      <span data-field="type">{leg.option_type}</span>{' '}
                      <span data-field="strike">{formatAmount(leg.strike_price, 2)}</span>
                    </span>
                    {/* An Asian leg is badged, not merely stored.
                        `option_type` says CALL for both a vanilla and an
                        average-price contract, and those are two different
                        instruments with two different payoffs -- a position
                        that reads as four vanilla calls when one of them is
                        Asian is misread, and the whole reason the engine
                        refuses to model it is that the difference is
                        invisible in every number on this row. */}
                    {leg.asian_type && leg.asian_type !== 'NOT_ASIAN' && (
                      <span
                        className="chip chip-accent"
                        title={
                          leg.asian_type === 'AVERAGE_PRICE'
                            ? 'Asian, average price: pays max(A − K, 0) on the realised average A, not on the price at expiry.'
                            : 'Asian, average strike: struck at the realised average A, not at a fixed K.'
                        }
                      >
                        {leg.asian_type === 'AVERAGE_PRICE' ? 'ASIAN·AVG PX' : 'ASIAN·AVG K'}
                      </span>
                    )}
                    <span className="leg-expiry" data-field="expiry">{expiryText(leg)}</span>
                    <button
                      className="btn leg-remove"
                      onClick={() => {
                        removeLeg(leg.id);
                        calculateStrategy();
                      }}
                      aria-label={`Remove ${side.toLowerCase()} ${leg.option_type.toLowerCase()} ${formatAmount(leg.strike_price, 2)}`}
                    >
                      ×
                    </button>
                  </div>

                  <div className="leg-fields">
                    <label className="leg-field leg-field--qty">
                      <span className="stat-label">{isOption ? 'Contracts' : 'Quantity'}</span>
                      <input
                        className="input"
                        style={{ textAlign: 'right' }}
                        type="number"
                        min={1}
                        value={leg.quantity}
                        aria-label="Quantity (contracts)"
                        onChange={(e) => {
                          updateLeg(leg.id, { quantity: Math.max(1, Number(e.target.value) || 1) });
                          calculateStrategy();
                        }}
                      />
                    </label>
                    <label className="leg-field leg-field--price">
                      <span className="stat-label">Price</span>
                      {/*
                        Grouped, and WITHOUT a symbol. A premium is usually a
                        two-figure per-share number, but it is multiplied by
                        quantity x 100 downstream, so a slipped digit is a
                        position priced an order of magnitude wrong.
                      */}
                      <MoneyInput
                        value={leg.premium}
                        onChange={(v) => {
                          updateLeg(leg.id, { premium: Number.isNaN(v) ? 0 : v });
                          calculateStrategy();
                        }}
                        decimals={2}
                        showSymbol={false}
                        ariaLabel="Leg price"
                        className="input"
                      />
                    </label>
                    {/* Per-leg risk. The aggregate cannot say which leg carries
                        the position: a condor nets to a small delta while each
                        wing holds a much larger one. An em dash until the
                        engine has answered, never zero -- a leg with no
                        computed risk and a leg with none are different states. */}
                    <span className="leg-risk" aria-label="Leg risk">
                      <span title="This leg's delta, in share-equivalents">Δ {risk ? risk.delta.toFixed(2) : '—'}</span>
                      <span title="This leg's decay, in dollars per calendar day">
                        Θ{' '}
                        <span className={risk && risk.theta < 0 ? 'loss' : risk ? 'profit' : undefined}>
                          {risk ? risk.theta.toFixed(2) : '—'}
                        </span>
                        /day
                      </span>
                      <span title="Model value against what this leg cost">
                        Open{' '}
                        <span className={risk ? (risk.openPnl >= 0 ? 'profit' : 'loss') : undefined}>
                          {risk ? `${risk.openPnl >= 0 ? '+' : ''}${formatAmount(risk.openPnl, 0)}` : '—'}
                        </span>
                      </span>
                    </span>
                  </div>
                </li>
              );
            })}
          </ul>
        )}
      </div>
    </div>
  );
}

export default PositionLegs;
