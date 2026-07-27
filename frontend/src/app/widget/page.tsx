'use client';

import React, { Suspense, useEffect } from 'react';
import { useSearchParams } from 'next/navigation';
import { StrategySelector } from '../../components/StrategySelector';
import OptionChain from '../../components/OptionChain';
import ProbabilityCurve from '../../components/ProbabilityCurve';
import StrategyMetrics from '../../components/StrategyMetrics';
import { useCalculatorStore } from '../../store/useCalculatorStore';

/**
 * Embeddable widget.
 *
 * Uses the same components and the same design tokens as the full workspace —
 * the `theme` query parameter simply sets the same `data-theme` attribute the
 * in-app toggle uses. The previous version injected a parallel glassmorphism
 * stylesheet through `dangerouslySetInnerHTML` and rendered a probability
 * panel hardcoded to a mean of 100.
 */
function WidgetContent() {
  const searchParams = useSearchParams();
  const theme = searchParams.get('theme') === 'light' ? 'light' : 'slate';
  const symbolParam = searchParams.get('symbol');

  const { setSymbol, symbol, calculateStrategy, legs } = useCalculatorStore();

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
  }, [theme]);

  useEffect(() => {
    setSymbol(symbolParam?.toUpperCase() || symbol);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [symbolParam]);

  useEffect(() => {
    if (legs.length > 0) calculateStrategy();
  }, [legs, calculateStrategy]);

  return (
    <div
      style={{
        minHeight: '100vh',
        padding: '0.625rem',
        display: 'flex',
        flexDirection: 'column',
        gap: '0.5rem',
      }}
    >
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          paddingBottom: '0.4375rem',
          borderBottom: '1px solid var(--color-line)',
        }}
      >
        <span style={{ fontSize: 'var(--text-base)', fontWeight: 600, color: 'var(--color-ink-100)' }}>
          Strategy Modeler
        </span>
        <span className="chip">Powered by sensen</span>
      </div>

      <div
        className="stagger"
        style={{
          display: 'grid',
          gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))',
          gap: '0.5rem',
        }}
      >
        <StrategySelector />
        <StrategyMetrics />
      </div>

      <div className="stagger" style={{ display: 'flex', flexDirection: 'column', gap: '0.5rem' }}>
        <div style={{ height: 340, display: 'flex' }}>
          <ProbabilityCurve />
        </div>
        <div style={{ height: 380, display: 'flex' }}>
          <OptionChain />
        </div>
      </div>
    </div>
  );
}

export default function WidgetPage() {
  return (
    <Suspense fallback={<div className="empty-state">Loading widget…</div>}>
      <WidgetContent />
    </Suspense>
  );
}
