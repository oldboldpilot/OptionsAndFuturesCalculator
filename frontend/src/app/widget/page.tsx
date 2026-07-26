'use client';

import React, { Suspense } from 'react';
import { useSearchParams } from 'next/navigation';
import { StrategySelector } from '../../components/StrategySelector';
import OptionChain from '../../components/OptionChain';
import OptionsHeatmap from '../../components/OptionsHeatmap';
import RiskMetrics from '../../components/RiskMetrics';

function WidgetContent() {
  const searchParams = useSearchParams();
  const theme = searchParams.get('theme') === 'light' ? 'light' : 'dark';
  const primaryColorParam = searchParams.get('primaryColor');
  const primaryColor = typeof primaryColorParam === 'string' ? `#${primaryColorParam}` : '#38bdf8';

  return (
    <div 
      style={{
        backgroundColor: theme === 'dark' ? '#0f172a' : '#ffffff',
        color: theme === 'dark' ? '#f8fafc' : '#0f172a',
        fontFamily: 'Inter, sans-serif',
        padding: '1rem',
        minHeight: '100vh',
        display: 'flex',
        flexDirection: 'column',
        gap: '1rem'
      }}
    >
      <style dangerouslySetInnerHTML={{__html: `
        :root {
          --primary: ${primaryColor};
          --bg-glass: ${theme === 'dark' ? 'rgba(30, 41, 59, 0.7)' : 'rgba(241, 245, 249, 0.7)'};
          --text-primary: ${theme === 'dark' ? '#f8fafc' : '#0f172a'};
          --text-secondary: ${theme === 'dark' ? '#94a3b8' : '#64748b'};
        }
        .glass-panel {
          background: var(--bg-glass);
          border: 1px solid ${theme === 'dark' ? 'rgba(255,255,255,0.1)' : 'rgba(0,0,0,0.1)'};
          border-radius: 12px;
        }
      `}} />
      
      {/* Widget Header area */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', paddingBottom: '0.5rem', borderBottom: `1px solid ${theme === 'dark' ? 'rgba(255,255,255,0.1)' : 'rgba(0,0,0,0.1)'}` }}>
        <h2 style={{ fontSize: '1.25rem', fontWeight: 600, color: 'var(--text-primary)' }}>Strategy Modeler</h2>
        <div style={{ fontSize: '0.75rem', color: 'var(--text-secondary)' }}>Powered by Sensen</div>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
        {/* Top Controls */}
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(300px, 1fr))', gap: '1rem' }}>
          <StrategySelector />
          <RiskMetrics />
        </div>

        {/* Heatmap Section */}
        <div className="glass-panel" style={{ height: '400px', display: 'flex', flexDirection: 'column' }}>
          <div style={{ padding: '1rem', borderBottom: '1px solid rgba(255,255,255,0.05)' }}>
            <h3 style={{ fontSize: '1rem', fontWeight: 600 }}>P&L Heatmap</h3>
          </div>
          <div style={{ flexGrow: 1, position: 'relative' }}>
            <OptionsHeatmap />
          </div>
        </div>

        {/* Option Chain */}
        <OptionChain />
      </div>
    </div>
  );
}

export default function WidgetPage() {
  return (
    <Suspense fallback={<div>Loading Widget...</div>}>
      <WidgetContent />
    </Suspense>
  );
}
