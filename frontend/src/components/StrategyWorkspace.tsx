'use client';

import { useEffect } from 'react';
import TopBar from './TopBar';
import ProbabilityCurve from './ProbabilityCurve';
import PayoffLadder from './PayoffLadder';
import PositionLegs from './PositionLegs';
import StrategyMetrics from './StrategyMetrics';
import OptionChain from './OptionChain';
import { StrategySelector } from './StrategySelector';
import { useCalculatorStore } from '../store/useCalculatorStore';

/**
 * The calculator workspace.
 *
 * Three columns, ordered the way the work actually flows: pick the structure
 * and legs on the left, read the outcome in the centre, assess risk on the
 * right. The probability curve is the centre of gravity — it is where the
 * payoff and the odds of reaching it are visible at once — so it gets the
 * largest region rather than being demoted to a panel in a split view.
 *
 * Both the home route and every /calculator/[strategy] landing page render
 * this, so there is one calculator to maintain rather than two that drift.
 */
export function StrategyWorkspace({ heading }: { heading?: string }) {
  const { calculateStrategy, legs, setSymbol, symbol } = useCalculatorStore();

  // Fetch the opening quote and chain once on mount.
  useEffect(() => {
    setSymbol(symbol);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    // Only compute once there is something to compute. Firing on an empty
    // position produced a meaningless "result" in the previous build.
    if (legs.length > 0) calculateStrategy();
  }, [legs, calculateStrategy]);

  const column: React.CSSProperties = {
    display: 'flex',
    flexDirection: 'column',
    gap: '0.5rem',
    minHeight: 0,
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100vh', overflow: 'hidden' }}>
      <TopBar />

      {heading && (
        <h1
          className="animate-fade"
          style={{
            fontSize: 'var(--text-base)',
            fontWeight: 600,
            color: 'var(--color-ink-200)',
            padding: '0.4375rem 0.75rem 0',
            flex: 'none',
          }}
        >
          {heading}
        </h1>
      )}

      <main
        style={{
          flex: 1,
          display: 'grid',
          gridTemplateColumns: 'minmax(290px, 330px) minmax(0, 1fr) minmax(255px, 300px)',
          gap: '0.5rem',
          padding: '0.5rem',
          minHeight: 0,
        }}
      >
        {/* Build */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          <StrategySelector />
          <PositionLegs />
        </div>

        {/* Read */}
        <div className="stagger" style={column}>
          <ProbabilityCurve />
          <div style={{ flex: '0 0 42%', minHeight: 0, display: 'flex' }}>
            <OptionChain />
          </div>
        </div>

        {/* Assess */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          <StrategyMetrics />
          <PayoffLadder />
        </div>
      </main>
    </div>
  );
}

export default StrategyWorkspace;
