'use client';

import { useMemo, useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';

type ValueMode = 'dollars' | 'percent';

/**
 * Payoff at the curve date, priced row by row.
 *
 * `StrategyResponse.pnl_matrix` is a one-dimensional curve — P&L against
 * underlying price on a single date — so that is what this renders. That date
 * is `curve_days_to_expiration`, the earliest leg expiry, which is why the
 * chip above the table names it rather than saying "at expiry" flatly. The
 * previous component presented this as the optionsprofitcalculator price ×
 * date grid by stamping every cell with `days_to_expiration: 30`, a constant
 * invented during mapping. The real date axis is `StrategyResponse.matrix`,
 * which the engine now fills correctly; wiring it up here is separate work.
 */
export function PayoffLadder() {
  const { result, spotPrice, isLoading, error } = useCalculatorStore();
  const [mode, setMode] = useState<ValueMode>('dollars');

  /**
   * "at expiry" was true only while every position had one expiry. The engine
   * draws this curve at the EARLIEST leg expiry, so on a calendar or diagonal
   * the flat label named a date the curve was not drawn at — and the shape it
   * describes belongs to the near leg's expiry, not the far one.
   */
  const curveLabel = useMemo(() => {
    if (!result) return { text: 'at expiry', title: '' };
    const { days, curveDays } = result.inputs;
    if (curveDays >= days) {
      return { text: `at expiry · ${Math.round(curveDays)}d`, title: 'Every leg expires on this date.' };
    }
    return {
      text: `at near expiry · ${Math.round(curveDays)}d`,
      title:
        `Drawn at the earliest leg expiry (${Math.round(curveDays)} days), not the ` +
        `position horizon (${Math.round(days)} days). Legs still open on that date ` +
        `are carried at their Black-Scholes value; legs already settled are ` +
        `carried at intrinsic against the price shown.`,
    };
  }, [result]);

  const rows = useMemo(() => {
    const curve = result?.expiryCurve ?? [];
    if (curve.length === 0) return null;

    // Descending price, the way every options tool renders a ladder.
    const sorted = [...curve].sort((a, b) => b.price - a.price);

    // Thin to a readable number of rows. Sampling every step-th row alone
    // drops the last one whenever the length is not a multiple of the step,
    // which silently hides the worst price on the ladder — so the final row is
    // always re-attached.
    const MAX = 25;
    const step = Math.max(1, Math.ceil(sorted.length / MAX));
    const thinned = sorted.filter((_, i) => i % step === 0);
    const last = sorted[sorted.length - 1];
    if (thinned[thinned.length - 1] !== last) thinned.push(last);

    const magnitude = Math.max(...curve.map((c) => Math.abs(c.pnl)), 1e-9);

    // The row closest to spot, so the eye lands on "where we are now".
    const spotRow = thinned.reduce((best, cur) =>
      Math.abs(cur.price - spotPrice) < Math.abs(best.price - spotPrice) ? cur : best,
    thinned[0]);

    return { thinned, magnitude, spotRow };
  }, [result, spotPrice]);

  const risk = Math.abs(result?.max_loss ?? 0);

  function label(pnl: number) {
    if (mode === 'percent') {
      if (risk < 1e-9) return '—';
      const p = (pnl / risk) * 100;
      return `${p > 0 ? '+' : ''}${p.toFixed(0)}%`;
    }
    const abs = Math.abs(pnl);
    const compact = abs >= 1000 ? `${(pnl / 1000).toFixed(1)}k` : pnl.toFixed(0);
    return pnl > 0 ? `+${compact}` : compact;
  }

  return (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.4375rem' }}>
          <span className="panel-title">Payoff</span>
          <span className="chip" title={curveLabel.title}>{curveLabel.text}</span>
        </div>
        <div className="segment">
          <button className="segment-item" data-active={mode === 'dollars'} onClick={() => setMode('dollars')}>$</button>
          <button className="segment-item" data-active={mode === 'percent'} onClick={() => setMode('percent')}>% risk</button>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1 }}>
        {error ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Unavailable</span>
            <span>{error}</span>
          </div>
        ) : isLoading ? (
          <div style={{ padding: '0.5rem', display: 'flex', flexDirection: 'column', gap: '3px' }}>
            {Array.from({ length: 8 }, (_, i) => (
              <div key={i} className="skeleton" style={{ height: 15 }} />
            ))}
          </div>
        ) : !rows ? (
          <div className="empty-state">
            <span className="empty-state-title">No payoff yet</span>
            <span>Add priced legs to compute the expiry curve.</span>
          </div>
        ) : (
          <table className="grid-table">
            <thead>
              <tr>
                <th style={{ textAlign: 'left' }}>Price</th>
                <th>P&amp;L</th>
                <th style={{ width: '46%' }} />
              </tr>
            </thead>
            <tbody>
              {rows.thinned.map((r, i) => {
                const isSpot = r === rows.spotRow;
                const frac = Math.min(Math.abs(r.pnl) / rows.magnitude, 1);
                const positive = r.pnl > 0;
                return (
                  <tr
                    key={r.price}
                    className="animate-fade"
                    style={{
                      animationDelay: `${Math.min(i * 0.012, 0.3)}s`,
                      background: isSpot ? 'var(--color-atm-tint)' : undefined,
                    }}
                  >
                    <td
                      style={{
                        textAlign: 'left',
                        color: isSpot ? 'var(--color-accent)' : 'var(--color-ink-200)',
                        fontWeight: isSpot ? 600 : 400,
                      }}
                    >
                      {r.price.toFixed(2)}
                    </td>
                    <td className={positive ? 'profit' : r.pnl < 0 ? 'loss' : 'flat'}>
                      {label(r.pnl)}
                    </td>
                    <td style={{ padding: '0.25rem 0.4375rem' }}>
                      {/* Diverging bar from a centre line: direction and
                          magnitude read without parsing the number. */}
                      <div style={{ position: 'relative', height: 7 }}>
                        <div
                          style={{
                            position: 'absolute',
                            left: '50%',
                            top: 0,
                            bottom: 0,
                            width: 1,
                            background: 'var(--color-line-strong)',
                          }}
                        />
                        <div
                          className="meter-fill"
                          style={{
                            position: 'absolute',
                            top: 1,
                            height: 5,
                            borderRadius: 2,
                            left: positive ? '50%' : `${50 - frac * 50}%`,
                            width: `${frac * 50}%`,
                            background: positive ? 'var(--color-profit)' : 'var(--color-loss)',
                            animationDelay: `${Math.min(i * 0.012, 0.3)}s`,
                          }}
                        />
                      </div>
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

export default PayoffLadder;
