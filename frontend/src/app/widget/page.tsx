'use client';

import React, { Suspense, useEffect } from 'react';
import { useSearchParams } from 'next/navigation';
import { StrategySelector } from '../../components/StrategySelector';
import OptionChain from '../../components/OptionChain';
import ProbabilityCurve from '../../components/ProbabilityCurve';
import StrategyMetrics from '../../components/StrategyMetrics';
import ResultsStrip from '../../components/ResultsStrip';
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
        <h1 style={{ fontSize: 'var(--text-base)', fontWeight: 600, color: 'var(--color-ink-100)' }}>
          Strategy Modeler
        </h1>
        {/*
          Was "Powered by sensen". This widget is embedded on other people's
          sites, so it is the most externally visible surface here, and the name
          of an internal engine means nothing to anyone reading it there.
        */}
        <span className="chip">Live market data</span>
      </div>

      {/* The headline numbers, as on the full calculator -- and the one place the
          entitlement notice is shown, which every panel below points at. In an
          embed it links out rather than starting a checkout inside the frame. */}
      <ResultsStrip embedded />

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
    // The fallback is what the static export ships, because the real content
    // reads the query string and so renders only in the browser. It names the
    // frame for a screen reader from the first byte, rather than the page having
    // no heading at all until scripts have run.
    <Suspense
      fallback={
        <div style={{ padding: '0.625rem' }}>
          <h1 style={{ fontSize: 'var(--text-base)', fontWeight: 600, color: 'var(--color-ink-100)' }}>
            Strategy Modeler
          </h1>
          <div className="empty-state">Loading widget…</div>
        </div>
      }
    >
      <WidgetContent />
    </Suspense>
  );
}
