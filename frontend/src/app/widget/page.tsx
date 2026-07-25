import React from 'react';
import { Metadata } from 'next';
import { StrategySelector } from '../../components/StrategySelector';
import OptionChain from '../../components/OptionChain';
import OptionsHeatmap from '../../components/OptionsHeatmap';
import RiskMetrics from '../../components/RiskMetrics';

// Widget Metadata
export const metadata: Metadata = {
  title: 'Options Calculator Widget',
  description: 'Embeddable White-Label Options & Futures Calculator',
  robots: 'noindex, nofollow', // Prevent search engines from indexing the iframe directly
};

export default async function WidgetPage({
  searchParams,
}: {
  searchParams: Promise<{ [key: string]: string | string[] | undefined }>;
}) {
  const resolvedSearchParams = await searchParams;
  // Extract styling or tenant configuration from query params
  // Examples: ?theme=light, ?primaryColor=006cfa, ?tenant=RIA_123
  const theme = resolvedSearchParams.theme === 'light' ? 'light' : 'dark';
  const primaryColor = typeof resolvedSearchParams.primaryColor === 'string' ? `#${resolvedSearchParams.primaryColor}` : '#38bdf8';
  
  // A completely stripped-down layout with no headers, sidebars, or broker routing (unless white-labeled)
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

        {/* Option Chain (Optional for widget, but keeping for completeness) */}
        <OptionChain />
      </div>
    </div>
  );
}
