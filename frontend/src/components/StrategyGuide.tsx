import React from 'react';
import type { StrategyGuide as Guide } from '@/content/strategy-guides';

/**
 * The written guide that sits below the calculator on each strategy page.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Deliberately set as an ARTICLE rather than as more terminal. The workspace
 * above it is 11px monospace at terminal density because it is operated; this
 * is prose at a comfortable measure because it is read. A page that renders its
 * only publisher content in the same dense grid as the strike ladder is a page
 * whose content nobody reads, which defeats the reason it was written.
 *
 * The column is capped at 46rem to match `LegalPage` — the other surface here
 * meant for reading — so the two agree rather than each inventing a measure.
 */
export function StrategyGuide({ guide }: { guide: Guide }) {
  return (
    <article
      id="guide"
      style={{
        maxWidth: '46rem',
        margin: '0 auto',
        padding: '3rem 1.5rem 1rem',
        color: 'var(--color-ink-200)',
        fontSize: '0.9375rem',
        lineHeight: 1.7,
      }}
    >
      <header>
        <h2 style={h2}>{guide.name}: how the position works</h2>
        <p style={{ margin: '0 0 1.5rem', fontSize: '1.0625rem', lineHeight: 1.65 }}>
          {guide.lede}
        </p>
      </header>

      {/*
        The identities, as a definition list rather than a table. These are the
        numbers somebody came here to check, so they sit above the discussion
        rather than beneath it.
      */}
      {/* A <dl>, because each Row is a <dt>/<dd> pair and those are only valid
          inside one. Rendering them under a <div> validates as nothing and
          gives assistive technology no term/definition relationship at all. */}
      <dl aria-label="Payoff at a glance" style={card}>
        <Row term="Market view" def={guide.outlook} />
        <Row term="Opened for" def={guide.netCost} />
        <Row term="Maximum profit" def={guide.maxProfit} />
        <Row term="Maximum loss" def={guide.maxLoss} />
        <Row term="Breakeven" def={guide.breakeven} />
      </dl>

      <h3 style={h3}>How it is built</h3>
      <ul style={list}>
        {guide.construction.map((leg) => (
          <li key={leg} style={li}>{leg}</li>
        ))}
      </ul>
      <p style={p}>
        All figures above are quoted <strong>per share</strong> and settle at expiry. A standard
        equity option covers 100 shares, so multiply by 100 for a single contract.
      </p>

      <h3 style={h3}>Before expiry: the Greeks</h3>
      <p style={p}>{guide.greeks}</p>

      <h3 style={h3}>When to use it</h3>
      <p style={p}>{guide.whenToUse}</p>

      <h3 style={h3}>What goes wrong</h3>
      <ul style={list}>
        {guide.risks.map((risk) => (
          <li key={risk} style={li}>{risk}</li>
        ))}
      </ul>

      <h3 style={h3}>A worked example</h3>
      <p style={{ ...p, fontStyle: 'italic', color: 'var(--color-ink-300)' }}>
        {guide.example.setup}
      </p>
      <dl style={card}>
        {guide.example.rows.map(([label, value]) => (
          <Row key={label} term={label} def={value} mono />
        ))}
      </dl>
      <p style={{ ...p, fontSize: '0.8125rem', color: 'var(--color-ink-400)' }}>
        {guide.example.note}
      </p>

      <h3 style={h3}>Common questions</h3>
      {guide.faqs.map((faq) => (
        <div key={faq.q} style={{ marginBottom: '1.25rem' }}>
          <h4
            style={{
              fontSize: '0.9375rem',
              fontWeight: 600,
              color: 'var(--color-ink-100)',
              margin: '0 0 0.375rem',
            }}
          >
            {faq.q}
          </h4>
          <p style={{ margin: 0 }}>{faq.a}</p>
        </div>
      ))}

      {/*
        Restated here rather than left to the site footer. Somebody who lands on
        this page from a search reads the strategy section and acts on it; the
        limits belong beside the numbers, not only at the bottom of the document.
      */}
      <p
        style={{
          margin: '2rem 0 0',
          paddingTop: '1.25rem',
          borderTop: '1px solid var(--color-line)',
          fontSize: '0.8125rem',
          color: 'var(--color-ink-400)',
        }}
      >
        <strong style={{ color: 'var(--color-ink-300)' }}>Educational only.</strong> This page
        explains how a structure behaves; it is not a recommendation to trade it. Options and
        futures carry substantial risk, and short and leveraged positions can lose more than the
        amount originally invested.
      </p>
    </article>
  );
}

function Row({ term, def, mono }: { term: string; def: string; mono?: boolean }) {
  return (
    <div
      style={{
        display: 'grid',
        gridTemplateColumns: 'minmax(9rem, 13rem) 1fr',
        gap: '0.5rem 1rem',
        padding: '0.625rem 0',
        borderBottom: '1px solid var(--color-line)',
      }}
    >
      <dt
        style={{
          fontSize: '0.75rem',
          textTransform: 'uppercase',
          letterSpacing: '0.06em',
          color: 'var(--color-ink-400)',
        }}
      >
        {term}
      </dt>
      <dd
        style={{
          margin: 0,
          fontFamily: mono ? 'var(--font-jetbrains-mono), monospace' : undefined,
          fontSize: mono ? '0.875rem' : undefined,
          color: 'var(--color-ink-100)',
        }}
      >
        {def}
      </dd>
    </div>
  );
}

const h2: React.CSSProperties = {
  fontFamily: 'var(--font-fraunces), Georgia, serif',
  fontSize: 'clamp(1.5rem, 3.5vw, 2rem)',
  lineHeight: 1.2,
  margin: '0 0 0.75rem',
  color: 'var(--color-ink-100)',
};

const h3: React.CSSProperties = {
  fontSize: '1.0625rem',
  fontWeight: 600,
  color: 'var(--color-ink-100)',
  margin: '2rem 0 0.625rem',
};

// The bordered block the identity rows and the worked example sit in. Shared so
// the two read as the same kind of object -- both are "the numbers", one
// general and one worked.
const card: React.CSSProperties = {
  margin: '0 0 1rem',
  padding: '0.25rem 1rem',
  border: '1px solid var(--color-line)',
  borderRadius: 'var(--radius-sm)',
  background: 'var(--color-base-700)',
};

const p: React.CSSProperties = { margin: '0 0 1rem' };
const list: React.CSSProperties = { margin: '0 0 1rem', paddingLeft: '1.125rem' };
const li: React.CSSProperties = { marginBottom: '0.5rem' };

export default StrategyGuide;
