'use client';

import { useEffect } from 'react';
import { PnLHeatmap } from '../components/PnLHeatmap';
import { DashboardLayout } from '../components/DashboardLayout';
import { StrategySelector } from '../components/StrategySelector';
import { OptionChainTable } from '../components/OptionChainTable';
import { useCalculatorStore } from '../store/useCalculatorStore';

export default function Home() {
  const { result, calculateStrategy, isLoading } = useCalculatorStore();

  useEffect(() => {
    // Scaffold initial render
    calculateStrategy();
  }, [calculateStrategy]);

  return (
    <DashboardLayout>
      <div style={{ 
        display: 'grid', 
        gridTemplateColumns: '1fr 1fr', 
        gap: '1.5rem', 
        height: '100%',
        paddingBottom: '2rem'
      }}>
        {/* Left Column: Strategy and Option Chain */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem', overflowY: 'auto', paddingRight: '0.5rem' }}>
          <StrategySelector />
          <OptionChainTable />
        </div>

        {/* Right Column: 3D Visualization */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div className="glass-panel" style={{ flexGrow: 1, minHeight: '600px', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
              <h2 style={{ fontSize: '1.25rem', fontWeight: 600, color: 'var(--text-primary)' }}>Risk Profile & P&L Heatmap</h2>
              <button className="btn" onClick={() => calculateStrategy()}>
                {isLoading ? 'Calculating...' : 'Recalculate'}
              </button>
            </div>
            <div style={{ flexGrow: 1, borderRadius: '8px', overflow: 'hidden', position: 'relative' }}>
              <PnLHeatmap data={result?.matrix || []} />
            </div>
          </div>
        </div>
      </div>
    </DashboardLayout>
  );
}
