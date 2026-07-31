'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';

/** Editable leg list. Strike, quantity and premium are all live-editable,
 *  matching optionsprofitcalculator.com's behaviour of letting the trader
 *  override any fill price. */
export function PositionLegs() {
  const { legs, updateLeg, removeLeg, clearLegs, calculateStrategy, result } =
    useCalculatorStore();

  return (
    <div className="panel">
      <div className="panel-head">
        <span className="panel-title">Position · {legs.length} leg{legs.length === 1 ? '' : 's'}</span>
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
          <table className="grid-table">
            <thead>
              <tr>
                <th style={{ textAlign: 'left' }}>Side</th>
                <th style={{ textAlign: 'left' }}>Type</th>
                <th>Strike</th>
                <th>Qty</th>
                <th>Premium</th>
                {/* Per-leg risk. The aggregate cannot say which leg carries the
                    position: a condor nets to a small delta while each wing
                    holds a much larger one, and closing the wrong side is how a
                    hedged book stops being hedged. */}
                <th title="This leg's delta, in share-equivalents">Δ</th>
                <th title="This leg's decay, in dollars per calendar day">Θ/day</th>
                <th title="Model value against what this leg cost">Open</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {legs.map((leg, i) => {
                const risk = result?.legRisk.find((r) => r.legIndex === i) ?? null;
                return (
                <tr key={leg.id}>
                  <td style={{ textAlign: 'left' }}>
                    <button
                      className={leg.action === 'BUY' ? 'btn btn-buy' : 'btn btn-sell'}
                      style={{ padding: '0.0625rem 0.375rem' }}
                      onClick={() => {
                        updateLeg(leg.id, {
                          action: leg.action === 'BUY' ? 'SELL' : 'BUY',
                        });
                        calculateStrategy();
                      }}
                    >
                      {leg.action}
                    </button>
                  </td>
                  <td
                    style={{
                      textAlign: 'left',
                      color:
                        leg.option_type === 'CALL'
                          ? 'var(--color-profit)'
                          : 'var(--color-loss)',
                    }}
                  >
                    {leg.option_type}
                  </td>
                  <td>{leg.strike_price.toFixed(2)}</td>
                  <td style={{ width: '52px' }}>
                    <input
                      className="input"
                      style={{ textAlign: 'right' }}
                      type="number"
                      min={1}
                      value={leg.quantity}
                      onChange={(e) => {
                        updateLeg(leg.id, {
                          quantity: Math.max(1, Number(e.target.value) || 1),
                        });
                        calculateStrategy();
                      }}
                    />
                  </td>
                  <td style={{ width: '68px' }}>
                    <input
                      className="input"
                      style={{ textAlign: 'right' }}
                      type="number"
                      step="0.01"
                      min={0}
                      value={leg.premium}
                      onChange={(e) => {
                        updateLeg(leg.id, { premium: Number(e.target.value) || 0 });
                        calculateStrategy();
                      }}
                    />
                  </td>
                  {/* Em dash until the engine has answered, never zero: a leg
                      with no computed risk and a leg with none are different
                      states and must not look alike. */}
                  <td>{risk ? risk.delta.toFixed(2) : '—'}</td>
                  <td className={risk && risk.theta < 0 ? 'loss' : risk ? 'profit' : undefined}>
                    {risk ? risk.theta.toFixed(2) : '—'}
                  </td>
                  <td className={risk ? (risk.openPnl >= 0 ? 'profit' : 'loss') : undefined}>
                    {risk ? `${risk.openPnl >= 0 ? '+' : ''}${risk.openPnl.toFixed(0)}` : '—'}
                  </td>
                  <td style={{ width: '24px' }}>
                    <button
                      className="btn"
                      style={{ padding: '0.0625rem 0.3125rem' }}
                      onClick={() => {
                        removeLeg(leg.id);
                        calculateStrategy();
                      }}
                      aria-label="Remove leg"
                    >
                      ×
                    </button>
                  </td>
                </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}

export default PositionLegs;
