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
    isLoading, error, legs, riskFreeRate, setRiskFreeRate, dividendYield, setDividendYield,
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
  // Same string-draft treatment as the rate: a number input bound straight
  // to state cannot hold an intermediate value like "1." while typing.
  const [divDraft, setDivDraft] = useState<string | null>(null);
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
        {/*
          The one place the display serif is used.

          Fraunces belongs on the ticker and nowhere else in this layout: it is
          the single identity-carrying text element on screen, it is set large
          enough for the optical-size axis to mean something, and it is a word
          rather than a figure. Everything adjacent to it — the price, the
          strikes, the Greeks — is a number that must hold a fixed advance
          width, which is exactly what a display serif does not do.
        */}
        <span
          style={{
            fontFamily: 'var(--font-display)',
            fontSize: 'var(--text-lg)',
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
        Quick-select, and specifically the reason the futures half of this
        product was invisible.

        The only way to reach a futures symbol was to already know that typing
        "ES" would work: the input is free text with no suggestions, and every
        futures strategy in the selector was refused until a futures symbol was
        loaded. So a visitor saw an options calculator with a permanently greyed
        Futures tab and no way in. These pills are the affordance that was
        missing -- the futures roots are named, so the term structure panel and
        the futures strategies become reachable by clicking rather than by
        guessing.

        The class is passed explicitly rather than inferred from the ticker
        because the root alone is ambiguous: ES is both the E-mini S&P future
        and Eversource Energy, and resolving it by guess is what previously put
        a utility's share price on an index future.
      */}
      <div
        style={{ display: 'flex', alignItems: 'center', gap: '0.75rem', flexWrap: 'wrap' }}
        aria-label="Quick symbol select"
      >
        {([
          { label: 'Equities', cls: 'EQUITY' as const, syms: ['SPY', 'QQQ', 'NVDA', 'AAPL', 'TSLA'] },
          { label: 'Futures', cls: 'FUTURES' as const, syms: ['ES', 'NQ', 'CL', 'GC', 'ZB'] },
        ]).map((group) => (
          <div key={group.label} style={{ display: 'flex', alignItems: 'center', gap: '0.25rem' }}>
            <span
              style={{
                fontSize: 'var(--text-2xs)',
                color: 'var(--color-ink-400)',
                textTransform: 'uppercase',
                letterSpacing: '0.05em',
              }}
            >
              {group.label}
            </span>
            {group.syms.map((sym) => (
              <button
                key={sym}
                type="button"
                onClick={() => setSymbol(sym, undefined, group.cls)}
                aria-pressed={symbol === sym}
                title={`${sym} — ${group.cls === 'FUTURES' ? 'futures root' : 'equity'}`}
                className="chip"
                style={{
                  cursor: 'pointer',
                  border: symbol === sym ? '1px solid var(--color-accent)' : '1px solid var(--color-line)',
                  color: symbol === sym ? 'var(--color-accent)' : 'var(--color-ink-300)',
                  background: 'transparent',
                }}
              >
                {sym}
              </button>
            ))}
          </div>
        ))}
      </div>

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

      {/*
        Continuous dividend yield.

        Deliberately labelled "assumed" and never "measured". The risk-free rate
        next to it carries a source, a tenor and an observation date because
        home.treasury.gov publishes one; no provider wired into this engine
        publishes a forward-looking continuous dividend yield, so this can only
        ever be a number somebody chose. Zero is the honest default — it means
        no dividend is modelled, and the engine's q == 0 path is bit-for-bit
        plain Black-Scholes rather than a dividend model fed a zero.
      */}
      <div style={{ display: 'flex', alignItems: 'baseline', gap: '0.3125rem' }}>
        <label
          htmlFor="divy"
          className={dividendYield > 0 ? 'chip chip-modelled' : 'chip'}
          title={
            dividendYield > 0
              ? `Assumed continuous dividend yield of ${(dividendYield * 100).toFixed(2)}% p.a. Priced by Black-Scholes-Merton at a discounted spot. This is a stated assumption, not measured data.`
              : 'No dividend modelled. Enter a continuous annual yield to price by Black-Scholes-Merton; it will be labelled an assumption.'
          }
        >
          {dividendYield > 0 ? 'div assumed' : 'no div'}
        </label>
        <input
          id="divy"
          className="input num"
          type="number"
          step="0.1"
          min="0"
          max="25"
          value={divDraft ?? (dividendYield === 0 ? '' : (dividendYield * 100).toFixed(2))}
          placeholder="0.00"
          onChange={(e) => {
            setDivDraft(e.target.value);
            const pct = parseFloat(e.target.value);
            setDividendYield(Number.isNaN(pct) || pct < 0 ? 0 : pct / 100);
          }}
          style={{ width: '58px', textAlign: 'right' }}
          aria-label="Dividend yield, percent per annum"
        />
        <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)' }}>% q</span>
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
