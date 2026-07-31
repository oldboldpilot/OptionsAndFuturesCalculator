'use client';

import { useEffect } from 'react';
import TopBar from './TopBar';
import ProbabilityCurve from './ProbabilityCurve';
import PayoffLadder from './PayoffLadder';
import PositionLegs from './PositionLegs';
import StrategyMetrics from './StrategyMetrics';
import OptionChain from './OptionChain';
import PnLMatrix from './PnLMatrix';
import OptionTicket from './OptionTicket';
import PnLSurface from './PnLSurface';
import AdSlot from './AdSlot';
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
          // Four columns, and the chain gets a full-height one of its own.
          // It was sharing a column and resolving to 256px — nine of 116 rows —
          // which is what made the ladder unusable regardless of how it was
          // sorted or scrolled. Reference data needs height more than anything
          // else on this page.
          gridTemplateColumns:
            'minmax(240px, 280px) minmax(280px, 320px) minmax(0, 1fr) minmax(300px, 380px)',
          gap: '0.5rem',
          padding: '0.5rem',
          minHeight: 0,
        }}
      >
        {/* Choose a structure */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          <StrategySelector />
        </div>

        {/* Compose the position: ticket first, then what it has built */}
        <div className="stagger" style={{ ...column, overflowY: 'auto' }}>
          <OptionTicket />
          <PositionLegs />
        </div>

        {/* Read the result */}
        <div className="stagger" style={column}>
          <ProbabilityCurve />
          {/* The matrix wants width — a dozen date columns plus the price axis —
              so it lives in the widest column and scrolls internally rather
              than forcing the page to scroll sideways. */}
          <div style={{ flex: '0 0 40%', minHeight: 0, display: 'flex' }}>
            <PnLMatrix />
          </div>
          <div style={{ flex: '0 0 30%', minHeight: 0, display: 'flex' }}>
            <PnLSurface />
          </div>
        </div>

        {/* Reference: the chain, at full height, opened at the money */}
        <div className="stagger" style={column}>
          <OptionChain />
          <div style={{ flex: '0 0 30%', minHeight: 0, display: 'flex' }}>
            <StrategyMetrics />
          </div>
          {/* Reserved from first paint, so an ad arriving late cannot shove the
              chain's buy and sell buttons out from under the cursor. */}
          <AdSlot size="rectangle" />
        </div>
      </main>
    </div>
  );
}

export default StrategyWorkspace;
