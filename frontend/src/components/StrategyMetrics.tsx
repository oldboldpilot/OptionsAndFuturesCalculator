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

  const greek = (label: string, unit: string, value: string) => (
    <div className="stat">
      <span className="stat-label">
        {label}{' '}
        <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>{unit}</span>
      </span>
      <span className="stat-value">{value}</span>
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

      {/*
        Units are part of the datum, not decoration.

        These arrive from the engine already converted to trader conventions —
        theta per calendar day, vega and rho per one point of vol and of rate —
        and already multiplied by contract multiplier and quantity, so they are
        position numbers rather than per-share ones. They were previously
        rendered bare, and the same theta printed as -21251.836: correct as
        dollars per year, unreadable as anything a trader acts on. Labelling
        them is the fix on this side; rescaling them here would double-convert
        what the engine already did (spec §3.4 — a value shown without its unit
        is not a value the user can verify).
      */}
      {section(
        'Greeks',
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: '0 0.75rem' }}>
          {greek('Delta Δ', 'shares', g.delta.toFixed(3))}
          {greek('Gamma Γ', 'Δ/$1', g.gamma.toFixed(4))}
          {greek('Theta Θ', '$/day', g.theta.toFixed(2))}
          {greek('Vega V', '$/1% IV', g.vega.toFixed(2))}
          {greek('Rho ρ', '$/1% rate', g.rho.toFixed(2))}
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
        Model inputs, shown because they are not all observations and the user
        must be able to tell which is which. Spot and implied volatility are
        measured — they come off the quote and the option chain. The risk-free
        rate is now measured too, from the Treasury par yield curve, and it is
        labelled with its tenor and observation date so it can be verified at
        source; when the feed is unavailable and the user states a rate instead,
        the same slot says so. It was previously an invisible constant that
        still shaped expected value, probability of profit and the whole
        distribution curve, which is exactly the invented figure spec §3.4
        forbids presenting as fact.
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
            {/* Two numbers when the legs expire on different dates: the payoff
                and every probability are measured at the near expiry, while the
                position itself runs to the far one. One number here would hide
                which of the two the panel above is describing. */}
            <span
              className="stat-value"
              title={
                result.inputs.curveDays < result.inputs.days
                  ? 'Near expiry (where the payoff and probabilities are measured) → position horizon'
                  : ''
              }
            >
              {result.inputs.curveDays < result.inputs.days
                ? `${Math.round(result.inputs.curveDays)} → ${Math.round(result.inputs.days)}`
                : Math.round(result.inputs.days)}
            </span>
          </div>
          <div className="stat">
            <span className="stat-label">
              Risk-free rate{' '}
              {result.inputs.rateSource === 'measured' && result.inputs.rateMeta ? (
                <span
                  className="chip chip-live"
                  title={`${result.inputs.rateMeta.tenor} US Treasury par yield (CMT) as of ${result.inputs.rateMeta.asOfDate}, source ${result.inputs.rateMeta.source}. Published ${(result.inputs.rateMeta.published * 100).toFixed(2)}% bond-equivalent; priced at ${(result.inputs.riskFreeRate * 100).toFixed(2)}% continuously compounded.`}
                >
                  CMT {result.inputs.rateMeta.tenor} · {result.inputs.rateMeta.asOfDate}
                </span>
              ) : (
                <span className="chip" title="A rate stated in the header, not observed market data.">
                  assumption
                </span>
              )}
            </span>
            {/*
              The rate the engine was given, not the published par yield. This
              section is titled "Model inputs" and this row is the model's r —
              rendering the bond-equivalent figure here stated a number the
              calculation did not use. The published figure remains in the chip
              tooltip above, which is where a user goes to reconcile against
              treasury.gov.
            */}
            <span className="stat-value">
              {(result.inputs.riskFreeRate * 100).toFixed(2)}%
            </span>
          </div>
        </div>,
      )}
    </div>,
  );
}

export default StrategyMetrics;
