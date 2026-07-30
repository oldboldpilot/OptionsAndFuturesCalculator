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
    isLoading, error, legs, riskFreeRate, setRiskFreeRate,
    rateSource, rateMeta, loadRiskFreeRate,
  } = useCalculatorStore();

  const [draft, setDraft] = useState('');

  // Fetched once, not from setSymbol: the risk-free rate is a property of the
  // market, not of the instrument, so it does not change when the symbol does.
  useEffect(() => {
    loadRiskFreeRate();
  }, [loadRiskFreeRate]);

  // The rate field keeps its own string draft. Binding a number input straight
  // to the store value makes the field impossible to clear and retype: an empty
  // string parses to NaN, the guard rejects it, and React snaps the old value
  // back mid-edit. Holding the text locally and only pushing valid numbers
  // through keeps typing natural while never letting NaN reach the store.
  const [rateDraft, setRateDraft] = useState<string | null>(null);
  // Show the rate the engine actually prices with — the continuously compounded
  // one. Showing the published par yield here instead was worse than a cosmetic
  // difference: the two are different numbers (3.83% bond-equivalent vs 3.7938%
  // continuous), and because this input is editable, any keystroke fed its own
  // displayed value back through setRiskFreeRate and re-entered the published
  // figure as if it were continuous — a silent 3.6bp move with no visible
  // cause, and the provenance chip flipping to ASSUMPTION for a number the user
  // never chose. The published figure is in the chip tooltip, where reading it
  // cannot change what is priced.
  const ratePct = rateDraft ?? (riskFreeRate === null ? '' : (riskFreeRate * 100).toFixed(2));

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

  // Treasury publishes a CMT print every business day, so the newest print is
  // at most one business day old. Four calendar days covers the widest ordinary
  // gap — a Friday print read on a Tuesday after a Monday holiday — and
  // anything beyond that means the feed stopped rather than that the calendar
  // was quiet.
  const rateIsStale = (() => {
    if (rateSource !== 'measured' || !rateMeta?.asOfDate) return false;
    const asOf = Date.parse(`${rateMeta.asOfDate}T00:00:00Z`);
    if (Number.isNaN(asOf)) return true;
    return (Date.now() - asOf) / 86_400_000 > 4;
  })();

  const rateChip = (() => {
    switch (rateSource) {
      case 'measured':
        // The observation date belongs in the chip, not only in the tooltip.
        // The backend serves its last good print indefinitely when the feed is
        // down, so a rate can be arbitrarily old while remaining a genuine
        // observation — styling that as live and hiding the date behind a hover
        // contradicts this file's own rule that provenance is chrome-level.
        return {
          text: `CMT ${rateMeta?.tenor ?? ''} · ${rateMeta?.asOfDate ?? ''}`,
          cls: rateIsStale ? 'chip chip-stale' : 'chip chip-live',
          title: rateMeta
            ? `${rateMeta.tenor} US Treasury par yield (CMT) as of ${rateMeta.asOfDate}, source ${rateMeta.source} (home.treasury.gov). Published ${(rateMeta.published * 100).toFixed(2)}% bond-equivalent; priced at ${((riskFreeRate ?? 0) * 100).toFixed(2)}% continuously compounded.${rateIsStale ? ' This print is older than the last business day — the feed may be stale.' : ''}`
            : '',
        };
      case 'user':
        return {
          text: 'ASSUMPTION',
          cls: 'chip',
          title: 'A rate you stated, not observed market data. Used exactly as entered.',
        };
      case 'unavailable':
        return {
          text: 'RATE UNAVAILABLE',
          cls: 'chip chip-stale',
          title: 'The Treasury feed did not answer, so no rate has been measured. Type one to proceed — it will be labelled an assumption.',
        };
      default:
        return { text: 'RATE…', cls: 'chip', title: 'Fetching the Treasury par yield curve.' };
    }
  })();

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

      {/*
        The risk-free rate and its provenance. It was a hardcoded 0.05 that
        still fed every engine request and the distribution's drift; it is now
        measured, and the chip says whether the number on screen is an
        observation with a date behind it or something a user typed. The field
        stays editable in every state, including when the feed is down: the user
        invoking the fallback is what makes it an assumption, and nothing here
        ever substitutes one silently.
      */}
      <div style={{ display: 'flex', alignItems: 'baseline', gap: '0.3125rem' }}>
        <label htmlFor="rfr" className={rateChip.cls} title={rateChip.title}>
          {rateChip.text}
        </label>
        <input
          id="rfr"
          className="input num"
          type="number"
          step="0.05"
          min="0"
          max="25"
          value={ratePct}
          placeholder="—"
          onChange={(e) => {
            setRateDraft(e.target.value);
            const pct = parseFloat(e.target.value);
            if (!Number.isNaN(pct) && pct >= 0) setRiskFreeRate(pct / 100);
          }}
          style={{ width: '64px', textAlign: 'right' }}
          aria-label="Risk-free rate, percent"
        />
        <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>% r</span>
      </div>

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
