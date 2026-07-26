import React from 'react';
import { Metadata } from 'next';
import { DashboardLayout } from '../../../components/DashboardLayout';
import { StrategySelector } from '../../../components/StrategySelector';
import { SymbolSelector } from '../../../components/SymbolSelector';
import OptionChain from '../../../components/OptionChain';
import OptionsHeatmap from '../../../components/OptionsHeatmap';
import RiskMetrics from '../../../components/RiskMetrics';
import ProbabilityDistribution from '../../../components/ProbabilityDistribution';
import { BrokerRouter } from '../../../components/BrokerRouter';

// List of strategies for SSG (Programmatic SEO)
const STRATEGIES = [
  'call-spread',
  'put-spread',
  'straddle',
  'strangle',
  'iron-condor',
  'butterfly',
  'covered-call',
  'futures-spread',
  'futures-outright',
  'futures-calendar-spread',
  'futures-intercommodity-spread',
  'covered-futures-call',
  'futures-basis-arbitrage'
];

interface Props {
  params: Promise<{ strategy: string }>;
}

// Generate Static Routes for each strategy at build time
export function generateStaticParams() {
  return STRATEGIES.map((strategy) => ({
    strategy: strategy,
  }));
}

// Dynamically generate SEO Metadata for each strategy page
export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const resolvedParams = await params;
  const strategyName = resolvedParams.strategy
    .split('-')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');
    
  return {
    title: `${strategyName} Options Calculator & Profit Visualizer`,
    description: `Calculate maximum profit, loss, and visualize the risk profile for a ${strategyName} using our advanced 3D Heatmap engine.`,
    openGraph: {
      title: `${strategyName} Calculator | Options & Futures`,
      description: `Model the PnL of a ${strategyName} strategy.`,
      // We will hook this up to the dynamic OG Edge function later
      images: [`/api/og?strategy=${resolvedParams.strategy}`],
    }
  };
}

export default async function StrategyCalculatorPage({ params }: Props) {
  const resolvedParams = await params;
  const strategyName = resolvedParams.strategy
    .split('-')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');

  return (
    <DashboardLayout>
      <div style={{ paddingBottom: '1rem' }}>
        <h1 style={{ fontSize: '1.75rem', fontWeight: 'bold', color: 'var(--text-primary)' }}>
          {strategyName} Calculator
        </h1>
        <p style={{ color: 'var(--text-secondary)' }}>
          Model, visualize, and execute your {strategyName} strategy.
        </p>
      </div>

      <div style={{ 
        display: 'grid', 
        gridTemplateColumns: '1fr 1fr', 
        gap: '1.5rem', 
        height: 'calc(100% - 4rem)',
        paddingBottom: '2rem'
      }}>
        {/* Left Column */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem', overflowY: 'auto', paddingRight: '0.5rem' }}>
          <SymbolSelector />
          <StrategySelector />
          <OptionChain />
          <BrokerRouter />
        </div>

        {/* Right Column */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '1.5rem' }}>
          <div className="glass-panel" style={{ flexGrow: 1, minHeight: '600px', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '1rem' }}>
              <h2 style={{ fontSize: '1.25rem', fontWeight: 600, color: 'var(--text-primary)' }}>Risk Profile & P&L Heatmap</h2>
            </div>
            <div style={{ flexGrow: 1, borderRadius: '8px', overflow: 'hidden', position: 'relative' }}>
              <OptionsHeatmap />
            </div>
          </div>
          
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
