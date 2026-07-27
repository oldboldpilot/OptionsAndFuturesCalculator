'use client';

import { useMemo, useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';

type ValueMode = 'dollars' | 'percent';

/**
 * The signature optionsprofitcalculator.com grid: underlying price down the
 * rows, calendar dates across the columns, each cell coloured by P&L.
 *
 * Per spec §3.4 this renders only values returned by the engine. When the
 * backend has produced no matrix we show an explicit empty state rather than
 * a plausible-looking placeholder grid.
 */
export function PnLMatrix() {
  const { result, spotPrice, isLoading, error } = useCalculatorStore();
  const [mode, setMode] = useState<ValueMode>('dollars');

  const grid = useMemo(() => {
    const cells = result?.matrix ?? [];
    if (cells.length === 0) return null;

    // Distinct axes, preserving numeric order: price descending (high at top,
    // as every options tool renders it), date ascending left-to-right.
    const prices = [...new Set(cells.map((c) => c.price))].sort((a, b) => b - a);
    const days = [...new Set(cells.map((c) => c.days_to_expiration))].sort(
      (a, b) => a - b,
    );

    const byKey = new Map<string, number>();
    for (const c of cells) {
      byKey.set(`${c.price}|${c.days_to_expiration}`, c.pnl_dollars);
    }

    // Symmetric scale so equal profit and loss read as equal intensity.
    const magnitude = Math.max(
      ...cells.map((c) => Math.abs(c.pnl_dollars)),
      1e-9,
    );

    return { prices, days, byKey, magnitude };
  }, [result]);

  const risk = Math.abs(result?.max_loss ?? 0);

  function cellStyle(pnl: number, magnitude: number) {
    // Non-linear ramp: sqrt keeps small P&L visible instead of washing out
    // everything that isn't near the extremes.
    const intensity = Math.min(Math.sqrt(Math.abs(pnl) / magnitude), 1);
    if (Math.abs(pnl) < 1e-9) {
      return { background: 'transparent', color: 'var(--color-ink-400)' };
    }
    const rgb = pnl > 0 ? '38, 166, 154' : '239, 83, 80';
    return {
      background: `rgba(${rgb}, ${(intensity * 0.42).toFixed(3)})`,
      color: intensity > 0.55 ? 'var(--color-ink-100)' : 'var(--color-ink-200)',
    };
  }

  function label(pnl: number) {
    if (mode === 'percent') {
      if (risk < 1e-9) return '—';
      const pct = (pnl / risk) * 100;
      return `${pct > 0 ? '+' : ''}${pct.toFixed(0)}%`;
    }
    const abs = Math.abs(pnl);
    const compact =
      abs >= 1000 ? `${(pnl / 1000).toFixed(1)}k` : pnl.toFixed(0);
    return pnl > 0 ? `+${compact}` : compact;
  }

  function dateLabel(daysOut: number) {
    const d = new Date();
    d.setDate(d.getDate() + daysOut);
    return d.toLocaleDateString(undefined, { month: 'short', day: 'numeric' });
  }

  return (
    <div className="panel" style={{ flex: 1, minHeight: 0 }}>
      <div className="panel-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
          <span className="panel-title">Profit / Loss Matrix</span>
          {grid && (
            <span className="chip">
              {grid.prices.length} × {grid.days.length}
            </span>
          )}
        </div>
        <div className="segment">
          <button
            className="segment-item"
            data-active={mode === 'dollars'}
            onClick={() => setMode('dollars')}
          >
            $
          </button>
          <button
            className="segment-item"
            data-active={mode === 'percent'}
            onClick={() => setMode('percent')}
          >
            % Risk
          </button>
        </div>
      </div>

      <div className="panel-body panel-body--flush" style={{ flex: 1 }}>
        {error ? (
          <div className="empty-state empty-state--error">
            <span className="empty-state-title">Calculation unavailable</span>
            <span>{error}</span>
          </div>
        ) : isLoading ? (
          <div className="empty-state">
            <span className="empty-state-title">Computing…</span>
          </div>
        ) : !grid ? (
          <div className="empty-state">
            <span className="empty-state-title">No matrix data</span>
            <span>
              Add legs and run a calculation. The engine returns no results
              until the backend is reachable.
            </span>
          </div>
        ) : (
          <table className="grid-table">
            <thead>
              <tr>
                <th
                  style={{
                    textAlign: 'left',
                    position: 'sticky',
                    left: 0,
                    zIndex: 3,
                  }}
                >
                  Price
                </th>
                {grid.days.map((d) => (
                  <th key={d}>
                    <div>{dateLabel(d)}</div>
                    <div
                      style={{
                        fontWeight: 400,
                        color: 'var(--color-ink-400)',
                      }}
                    >
                      {d}d
                    </div>
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {grid.prices.map((p) => {
                // Mark the row nearest spot so the trader can orient instantly.
                const isSpotRow =
                  grid.prices.reduce((best, cur) =>
                    Math.abs(cur - spotPrice) < Math.abs(best - spotPrice)
                      ? cur
                      : best,
                  ) === p;
                return (
                  <tr key={p}>
                    <td
                      style={{
                        textAlign: 'left',
                        position: 'sticky',
                        left: 0,
                        zIndex: 1,
                        background: isSpotRow
                          ? 'var(--color-atm-tint)'
                          : 'var(--color-base-800)',
                        color: isSpotRow
                          ? 'var(--color-accent)'
                          : 'var(--color-ink-200)',
                        fontWeight: isSpotRow ? 600 : 400,
                        borderRight: '1px solid var(--color-line)',
                      }}
                    >
                      {p.toFixed(2)}
                    </td>
                    {grid.days.map((d) => {
                      const pnl = grid.byKey.get(`${p}|${d}`);
                      if (pnl === undefined) {
                        return (
                          <td key={d} style={{ color: 'var(--color-ink-400)' }}>
                            —
                          </td>
                        );
                      }
                      return (
                        <td key={d} style={cellStyle(pnl, grid.magnitude)}>
                          {label(pnl)}
                        </td>
                      );
                    })}
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

export default PnLMatrix;
