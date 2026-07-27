'use client';

import { useEffect } from 'react';
import TopBar from '../components/TopBar';
import PnLMatrix from '../components/PnLMatrix';
import PositionLegs from '../components/PositionLegs';
import StrategyMetrics from '../components/StrategyMetrics';
import OptionChain from '../components/OptionChain';
import { StrategySelector } from '../components/StrategySelector';
import { useCalculatorStore } from '../store/useCalculatorStore';

/**
 * Workspace layout.
 *
 * Three columns, ordered the way the work actually flows: pick the structure
 * and legs on the left, read the outcome in the centre, check risk on the
 * right. The P&L matrix is the centre of gravity — it is the reason the tool
 * exists, so it gets the largest, tallest region rather than being demoted to
 * a panel in a split view.
 */
export default function Home() {
  const { calculateStrategy, legs } = useCalculatorStore();

  useEffect(() => {
    // Only compute once there is something to compute. Firing on an empty
    // position produced a meaningless "result" in the previous build.
    if (legs.length > 0) calculateStrategy();
  }, [legs, calculateStrategy]);

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        height: '100vh',
        overflow: 'hidden',
      }}
    >
      <TopBar />

      <main
        style={{
          flex: 1,
          display: 'grid',
          gridTemplateColumns: 'minmax(300px, 340px) minmax(0, 1fr) minmax(250px, 290px)',
          gap: '0.5rem',
          padding: '0.5rem',
          minHeight: 0,
        }}
      >
        {/* Build */}
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '0.5rem',
            minHeight: 0,
            overflowY: 'auto',
          }}
        >
          <StrategySelector />
          <PositionLegs />
        </div>

        {/* Read */}
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '0.5rem',
            minHeight: 0,
          }}
        >
          <PnLMatrix />
          <div style={{ flex: '0 0 40%', minHeight: 0, display: 'flex' }}>
            <OptionChain />
          </div>
        </div>

        {/* Assess */}
        <div
          style={{
            display: 'flex',
            flexDirection: 'column',
            gap: '0.5rem',
            minHeight: 0,
            overflowY: 'auto',
          }}
        >
          <StrategyMetrics />
        </div>
      </main>
    </div>
  );
}
