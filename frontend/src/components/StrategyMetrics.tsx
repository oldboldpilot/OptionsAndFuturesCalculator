'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';

const money = (v: number) =>
  `${v < 0 ? '-' : ''}$${Math.abs(v).toLocaleString(undefined, {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  })}`;

/**
 * Strategy-level outcome and Greeks.
 *
 * Every figure here comes from the engine response. When there is no result we
 * render an explicit empty state — never zeros, which would read as a genuine
 * computed outcome (spec §3.4).
 */
export function StrategyMetrics() {
  const { result, error } = useCalculatorStore();

  if (error) {
    return (
      <div className="panel">
        <div className="panel-head">
          <span className="panel-title">Strategy</span>
        </div>
        <div className="empty-state empty-state--error">
          <span className="empty-state-title">Unavailable</span>
          <span>{error}</span>
        </div>
      </div>
    );
  }

  if (!result) {
    return (
      <div className="panel">
        <div className="panel-head">
          <span className="panel-title">Strategy</span>
        </div>
        <div className="empty-state">
          <span className="empty-state-title">No result</span>
          <span>Run a calculation to see outcome and Greeks.</span>
        </div>
      </div>
    );
  }

  const g = result.aggregate_greeks;

  return (
    <div className="panel">
      <div className="panel-head">
        <span className="panel-title">Strategy</span>
      </div>
      <div className="panel-body">
        <div className="stat">
          <span className="stat-label">Max profit</span>
          <span className="stat-value profit">{money(result.max_profit)}</span>
        </div>
        <div className="stat">
          <span className="stat-label">Max loss</span>
          <span className="stat-value loss">{money(result.max_loss)}</span>
        </div>
        <div className="stat">
          <span className="stat-label">Risk / reward</span>
          <span className="stat-value">
            {result.risk_reward_ratio > 0
              ? `${result.risk_reward_ratio.toFixed(2)} : 1`
              : '—'}
          </span>
        </div>

        <div
          style={{
            marginTop: '0.625rem',
            paddingTop: '0.5rem',
            borderTop: '1px solid var(--color-line)',
          }}
        >
          <div
            className="panel-title"
            style={{ marginBottom: '0.375rem', display: 'block' }}
          >
            Greeks
          </div>
          <div
            style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(2, 1fr)',
              gap: '0 0.75rem',
            }}
          >
            <div className="stat">
              <span className="stat-label">Delta Δ</span>
              <span className="stat-value">{g.delta.toFixed(3)}</span>
            </div>
            <div className="stat">
              <span className="stat-label">Gamma Γ</span>
              <span className="stat-value">{g.gamma.toFixed(4)}</span>
            </div>
            <div className="stat">
              <span className="stat-label">Theta Θ</span>
              <span className="stat-value">{g.theta.toFixed(3)}</span>
            </div>
            <div className="stat">
              <span className="stat-label">Vega V</span>
              <span className="stat-value">{g.vega.toFixed(3)}</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default StrategyMetrics;
