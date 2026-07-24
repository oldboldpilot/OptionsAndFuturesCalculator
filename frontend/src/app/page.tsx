'use client';

import { PnLHeatmap } from '../components/PnLHeatmap';
import { useCalculatorStore } from '../store/useCalculatorStore';
import { useEffect } from 'react';

export default function Home() {
  const { result, calculateStrategy, isLoading } = useCalculatorStore();

  useEffect(() => {
    // Scaffold initial render
    calculateStrategy();
  }, [calculateStrategy]);

  return (
    <main style={{ padding: '4rem', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: '2rem' }}>
      <div className="glass-panel" style={{ maxWidth: '800px', width: '100%', textAlign: 'center' }}>
        <h1 className="heading-1">Options & Futures Profit Calculator</h1>
        <p style={{ color: 'var(--text-secondary)', marginBottom: '2rem', fontSize: '1.2rem' }}>
          Real-time, interactive profit-and-loss (P&L) matrix visualizations, risk probability models, and option sensitivity breakdowns.
        </p>
        
        <div style={{ display: 'flex', gap: '1rem', justifyContent: 'center' }}>
          <button className="btn" onClick={() => calculateStrategy()}>
            {isLoading ? 'Calculating...' : 'Recalculate Model'}
          </button>
          <button className="btn" style={{ background: 'transparent', border: '1px solid var(--glass-border)' }}>View Strategies</button>
        </div>
      </div>

      <div style={{ maxWidth: '1000px', width: '100%' }}>
        <PnLHeatmap data={result?.matrix || []} />
      </div>
    </main>
  );
}
