'use client';

import { useEffect, useRef, useState } from 'react';
import { useCalculatorStore } from '../store/useCalculatorStore';
import ThemeToggle from './ThemeToggle';

/**
 * Application header: instrument identity, live spot, and data provenance.
 *
 * Provenance is deliberately prominent. Per spec §3.4 the user must always be
 * able to tell whether a number is quoted, stale, or modelled — so quote
 * freshness is chrome-level information, not something buried in a tooltip.
 */
export function TopBar() {
  const {
    symbol, spotPrice, assetClass, setSymbol, calculateStrategy,
    isLoading, error, legs,
  } = useCalculatorStore();

  const [draft, setDraft] = useState('');

  // Flash the price when it moves, so an update is never missed. Direction is
  // carried by colour, which is why the previous value has to be remembered.
  const prev = useRef(spotPrice);
  const [flash, setFlash] = useState<'' | 'flash-profit' | 'flash-loss'>('');
  useEffect(() => {
    if (prev.current > 0 && spotPrice > 0 && spotPrice !== prev.current) {
      setFlash(spotPrice > prev.current ? 'flash-profit' : 'flash-loss');
      const t = setTimeout(() => setFlash(''), 750);
      prev.current = spotPrice;
      return () => clearTimeout(t);
    }
    prev.current = spotPrice;
  }, [spotPrice]);

  function submit(e: React.FormEvent) {
    e.preventDefault();
    const next = draft.trim().toUpperCase();
    if (next) {
      setSymbol(next);
      setDraft('');
    }
  }

  const quoteState: 'live' | 'unavailable' = spotPrice > 0 && !error ? 'live' : 'unavailable';

  return (
    <header
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '0.875rem',
        padding: '0.5rem 0.75rem',
        background: 'var(--color-base-800)',
        borderBottom: '1px solid var(--color-line)',
        boxShadow: 'var(--shadow-panel)',
        position: 'relative',
        zIndex: 5,
        flex: 'none',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'baseline', gap: '0.375rem' }}>
        <span
          style={{
            fontSize: 'var(--text-base)',
            fontWeight: 700,
            color: 'var(--color-ink-100)',
            letterSpacing: '-0.01em',
          }}
        >
          {symbol}
        </span>
        <span className="chip">{assetClass}</span>
      </div>

      <div style={{ display: 'flex', alignItems: 'baseline', gap: '0.4375rem' }}>
        {quoteState === 'live' ? (
          <span
            className={`num ${flash}`}
            style={{
              fontSize: 'var(--text-lg)',
              fontWeight: 600,
              color: 'var(--color-ink-100)',
              padding: '0 0.25rem',
              borderRadius: 'var(--radius-sm)',
            }}
          >
            {spotPrice.toFixed(2)}
          </span>
        ) : (
          <span className="num" style={{ fontSize: 'var(--text-lg)', color: 'var(--color-ink-400)' }}>
            —
          </span>
        )}
        <span className={quoteState === 'live' ? 'chip chip-live' : 'chip chip-stale'}>
          <i className="dot" />
          {quoteState === 'live' ? 'QUOTE' : 'NO QUOTE'}
        </span>
      </div>

      <form onSubmit={submit} style={{ width: '160px' }}>
        <input
          className="input"
          placeholder="Search symbol…"
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          aria-label="Search symbol"
        />
      </form>

      <div style={{ flex: 1 }} />

      {error && (
        <span className="chip" style={{ color: 'var(--color-loss)', borderColor: 'var(--color-loss-dim)' }} title={error}>
          {error.length > 46 ? `${error.slice(0, 46)}…` : error}
        </span>
      )}

      <ThemeToggle />

      <button
        className="btn btn-primary"
        onClick={() => calculateStrategy()}
        disabled={isLoading || legs.length === 0}
      >
        {isLoading ? 'Calculating…' : 'Recalculate'}
      </button>
    </header>
  );
}

export default TopBar;
