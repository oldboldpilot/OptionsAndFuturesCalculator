'use client';

import { useCalculatorStore } from '../store/useCalculatorStore';

const money = (v: number) =>
  `${v < 0 ? '−' : ''}$${Math.abs(v).toLocaleString(undefined, {
    minimumFractionDigits: 2,
    maximumFractionDigits: 2,
  })}`;

/**
 * Strategy-level outcome, probability and risk.
 *
 * Every figure comes from `StrategyResponse`. Break-even, expected value,
 * probability of profit and the parametric VaR/CVaR block were all being
 * returned by the engine and silently dropped during mapping — they are shown
 * here. When there is no result we render an explicit empty state rather than
 * zeros, which would read as a genuine computed outcome (spec §3.4).
 */
export function StrategyMetrics() {
  const { result, error } = useCalculatorStore();

  const shell = (body: React.ReactNode) => (
    <div className="panel" style={{ flex: 'none' }}>
      <div className="panel-head">
        <span className="panel-title">Strategy</span>
        {result && <span className="chip chip-accent">engine</span>}
      </div>
      {body}
    </div>
  );

  if (error) {
    return shell(
      <div className="empty-state empty-state--error">
        <span className="empty-state-title">Unavailable</span>
        <span>{error}</span>
      </div>,
    );
  }

  if (!result) {
    return shell(
      <div className="empty-state">
        <span className="empty-state-title">No result</span>
        <span>Run a calculation to see outcome, probability and Greeks.</span>
      </div>,
    );
  }

  const g = result.aggregate_greeks;
  const r = result.risk;
  const hasRisk = r.var95 !== 0 || r.var99 !== 0 || r.cvar95 !== 0 || r.cvar99 !== 0;

  const section = (title: string, children: React.ReactNode) => (
    <div style={{ marginTop: '0.625rem', paddingTop: '0.5rem', borderTop: '1px solid var(--color-line)' }}>
      <div className="panel-title" style={{ marginBottom: '0.375rem', display: 'block' }}>
        {title}
      </div>
      {children}
    </div>
  );

  return shell(
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
          {result.risk_reward_ratio > 0 ? `${result.risk_reward_ratio.toFixed(2)} : 1` : '—'}
        </span>
      </div>
      <div className="stat">
        <span className="stat-label">Break-even</span>
        <span className="stat-value">
          {result.break_even > 0 ? result.break_even.toFixed(2) : '—'}
        </span>
      </div>
      <div className="stat">
        <span className="stat-label">Expected value</span>
        <span className={`stat-value ${result.expected_value >= 0 ? 'profit' : 'loss'}`}>
          {money(result.expected_value)}
        </span>
      </div>
      <div className="stat">
        <span className="stat-label">Probability of profit</span>
        <span className="stat-value">
          {result.pop > 0 ? `${(result.pop * 100).toFixed(1)}%` : '—'}
        </span>
      </div>

      {section(
        'Greeks',
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '0 0.75rem' }}>
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
          <div className="stat">
            <span className="stat-label">Rho ρ</span>
            <span className="stat-value">{g.rho.toFixed(3)}</span>
          </div>
        </div>,
      )}

      {hasRisk
        ? section(
            'Tail risk (parametric)',
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '0 0.75rem' }}>
              <div className="stat">
                <span className="stat-label">VaR 95</span>
                <span className="stat-value loss">{money(r.var95)}</span>
              </div>
              <div className="stat">
                <span className="stat-label">VaR 99</span>
                <span className="stat-value loss">{money(r.var99)}</span>
              </div>
              <div className="stat">
                <span className="stat-label">CVaR 95</span>
                <span className="stat-value loss">{money(r.cvar95)}</span>
              </div>
              <div className="stat">
                <span className="stat-label">CVaR 99</span>
                <span className="stat-value loss">{money(r.cvar99)}</span>
              </div>
            </div>,
          )
        : section(
            'Tail risk (parametric)',
            <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>
              The engine returned no VaR/CVaR for this position.
            </div>,
          )}

      {/*
        Model inputs, shown because they are not all observations.
        Spot and implied volatility are measured — they come off the quote and
        the option chain. The risk-free rate does not: nothing in this system
        observes it, so it is a stated assumption. It was previously an
        invisible constant that still shaped expected value, probability of
        profit and the whole distribution curve, which is precisely the kind of
        invented figure spec §3.4 forbids presenting as fact. Disclosing it is
        the minimum; sourcing it is tracked as follow-up work.
      */}
      {section(
        'Model inputs',
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '0 0.75rem' }}>
          <div className="stat">
            <span className="stat-label">Spot</span>
            <span className="stat-value">{money(result.inputs.spot)}</span>
          </div>
          <div className="stat">
            <span className="stat-label">Implied vol</span>
            <span className="stat-value">{(result.inputs.impliedVolatility * 100).toFixed(1)}%</span>
          </div>
          <div className="stat">
            <span className="stat-label">Days</span>
            <span className="stat-value">{result.inputs.days}</span>
          </div>
          <div className="stat">
            <span className="stat-label">
              Risk-free rate{' '}
              <span className="chip" title="Not measured by this system — a stated model assumption, editable in the header.">
                assumption
              </span>
            </span>
            <span className="stat-value">{(result.inputs.riskFreeRate * 100).toFixed(2)}%</span>
          </div>
        </div>,
      )}
    </div>,
  );
}

export default StrategyMetrics;
