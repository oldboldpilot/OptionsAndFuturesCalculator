'use client';

import { useEffect } from 'react';
import OptionsHeatmap from '../components/OptionsHeatmap';
import { DashboardLayout } from '../components/DashboardLayout';
import { StrategySelector } from '../components/StrategySelector';
import OptionChain from '../components/OptionChain';
import { BrokerRouter } from '../components/BrokerRouter';
import RiskMetrics from '../components/RiskMetrics';
import ProbabilityDistribution from '../components/ProbabilityDistribution';
import { useCalculatorStore } from '../store/useCalculatorStore';

export default function Home() {
  const { calculateStrategy, isLoading } = useCalculatorStore();

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
          <OptionChain />
          <BrokerRouter />
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
              <OptionsHeatmap />
            </div>
          </div>
          
          {/* Risk & Probabilities Sub-Panel */}
          <div style={{ display: 'flex', gap: '1.5rem' }}>
             <div style={{ flex: 1 }}>
               <RiskMetrics />
             </div>
             <div style={{ flex: 1 }}>
               <ProbabilityDistribution mean={100} stdDev={15} />
             </div>
          </div>
        </div>
      </div>
    </DashboardLayout>
  );
}
