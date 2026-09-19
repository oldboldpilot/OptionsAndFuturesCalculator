import React from 'react';
import { STRATEGY_GUIDES } from '@/content/strategy-guides';
import { getCalculatorExtra } from '@/content/calculator-extras';

/**
 * The second block of per-strategy content on `/calculator/<slug>`, below the
 * lede from `calculator-pages.ts`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * See `calculator-extras.ts` for why this exists and what each block is
 * grounded in. Nothing here is prose lifted from the guide: the mechanics
 * paragraph is a different angle (execution, not payoff shape or view), the
 * tables are numbers, and the comparison table is COMPUTED from
 * `STRATEGY_GUIDES` rather than authored — the same underlying facts the
 * guide's own "Related strategies" grid uses, read out as a comparison
 * instead of a link grid.
 *
 * Below the workspace, same as the lede: the shell above is viewport-pinned.
 */
export function CalculatorPageExtras({ slug }: { slug: string }) {
  const extra = getCalculatorExtra(slug);
  const guide = STRATEGY_GUIDES[slug];
  if (!extra) return null;

  const legCount = extra.legs.length;

  return (
    <section
      data-strategy-extras={slug}
      style={{
        maxWidth: '60rem',
        margin: '0 auto',
        padding: '1.5rem 1.25rem 3rem',
        fontSize: '0.8125rem',
        lineHeight: 1.7,
        color: 'var(--color-ink-300)',
      }}
    >
      <p style={{ margin: '0 0 1.25rem' }}>{extra.mechanics}</p>

      <h2 style={h2}>Sizing and account notes</h2>
      <p style={{ margin: '0 0 1.25rem' }}>{extra.note}</p>

      <p
        style={{
          margin: '0 0 1.25rem',
          padding: '0.625rem 0.875rem',
          border: '1px solid var(--color-line)',
          borderRadius: 'var(--radius-sm)',
          background: 'var(--color-base-700)',
        }}
      >
        <strong style={{ color: 'var(--color-ink-100)' }}>Common mistake. </strong>
        {extra.mistake}
      </p>

      <h2 style={h2}>At a glance</h2>
      <dl style={card}>
        <Row term="Legs" def={String(legCount)} />
        {guide && <Row term="Market view" def={guide.outlook} />}
        {guide && <Row term="Opened for" def={guide.netCost} />}
        <Row term="What bounds the profit" def={extra.caps.profitCap} />
        <Row term="What bounds the loss" def={extra.caps.lossCap} />
      </dl>

      <h2 style={h2}>The order ticket, from the worked example</h2>
      <table style={table}>
        <thead>
          <tr>
            <th style={th}>#</th>
            <th style={th}>Action</th>
            <th style={th}>Instrument</th>
            <th style={th}>Strike</th>
          </tr>
        </thead>
        <tbody>
          {extra.legs.map((leg, i) => (
            <tr key={i}>
              <td style={td}>{i + 1}</td>
              <td style={td}>{leg.action}</td>
              <td style={td}>{leg.instrument}</td>
              <td style={td}>{leg.strike}</td>
            </tr>
          ))}
        </tbody>
      </table>

      {extra.grid && (
        <>
          <h2 style={h2}>Payoff at expiry, across a price grid</h2>
          <p style={{ margin: '0 0 0.75rem' }}>
            Computed from the same strikes and net premium as the worked example above — not a
            simulation, the closed-form payoff evaluated at each price.
          </p>
          <table style={table}>
            <thead>
              <tr>
                <th style={th}>{extra.grid.axisLabel}</th>
                <th style={th}>P&amp;L</th>
              </tr>
            </thead>
            <tbody>
              {extra.grid.rows.map((row) => (
                <tr key={row.x}>
                  <td style={td}>{row.x}</td>
                  <td style={{ ...td, fontFamily: 'var(--font-jetbrains-mono), monospace' }}>
                    {row.pnl}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}

      {extra.ratioTable && (
        <>
          <h2 style={h2}>Leg ratio</h2>
          <table style={table}>
            <thead>
              <tr>
                <th style={th}>Role</th>
                <th style={th}>Instrument</th>
                <th style={th}>Position</th>
              </tr>
            </thead>
            <tbody>
              {extra.ratioTable.map((row) => (
                <tr key={row.instrument}>
                  <td style={td}>{row.role}</td>
                  <td style={td}>{row.instrument}</td>
                  <td style={td}>{row.ratio}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}

      {extra.sensitivityTable && (
        <>
          <h2 style={h2}>Sensitivity to the assumption behind the example</h2>
          <table style={table}>
            <thead>
              <tr>
                <th style={th}>{extra.sensitivityTable.axisLabel}</th>
                <th style={th}>Implied carry profit</th>
              </tr>
            </thead>
            <tbody>
              {extra.sensitivityTable.rows.map((row) => (
                <tr key={row.label}>
                  <td style={td}>{row.label}</td>
                  <td style={{ ...td, fontFamily: 'var(--font-jetbrains-mono), monospace' }}>
                    {row.value}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}

      {extra.noClosedForm && (
        <p style={{ margin: '0.5rem 0 0', fontSize: '0.75rem', color: 'var(--color-ink-400)' }}>
          No price grid is shown here: this structure has no closed-form payoff at expiry — the
          back leg is still alive and has to be valued by a model. Use the payoff curve in the
          calculator above.
        </p>
      )}

      {guide?.related && guide.related.length > 0 && (
        <>
          <h2 style={h2}>How this compares with related strategies</h2>
          <table style={table}>
            <thead>
              <tr>
                <th style={th}>Strategy</th>
                <th style={th}>Market view</th>
                <th style={th}>Opened for</th>
                <th style={th}>Legs vs. this one</th>
              </tr>
            </thead>
            <tbody>
              {guide.related.map((relSlug) => {
                const relGuide = STRATEGY_GUIDES[relSlug];
                const relExtra = getCalculatorExtra(relSlug);
                if (!relGuide) return null;
                const relLegs = relExtra?.legs.length ?? relGuide.construction.length;
                const delta = relLegs - legCount;
                const deltaLabel =
                  delta === 0 ? 'same' : delta > 0 ? `+${delta}` : String(delta);
                return (
                  <tr key={relSlug}>
                    <td style={td}>
                      <a
                        href={`/calculator/${relSlug}`}
                        style={{ color: 'var(--color-accent)', textDecoration: 'none' }}
                      >
                        {relGuide.name}
                      </a>
                    </td>
                    <td style={td}>{relGuide.outlook}</td>
                    <td style={td}>{relGuide.netCost}</td>
                    <td style={td}>{deltaLabel}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </>
      )}
    </section>
  );
}

function Row({ term, def }: { term: string; def: string }) {
  return (
    <div
      style={{
        display: 'grid',
        gridTemplateColumns: 'minmax(9rem, 13rem) 1fr',
        gap: '0.5rem 1rem',
        padding: '0.5rem 0',
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
      <dd style={{ margin: 0, color: 'var(--color-ink-100)' }}>{def}</dd>
    </div>
  );
}

const h2: React.CSSProperties = {
  fontSize: '0.9375rem',
  fontWeight: 600,
  color: 'var(--color-ink-100)',
  margin: '1.75rem 0 0.625rem',
};

const card: React.CSSProperties = {
  margin: '0 0 1rem',
  padding: '0.25rem 1rem',
  border: '1px solid var(--color-line)',
  borderRadius: 'var(--radius-sm)',
  background: 'var(--color-base-700)',
};

const table: React.CSSProperties = {
  width: '100%',
  borderCollapse: 'collapse',
  marginBottom: '1rem',
  fontSize: '0.8125rem',
};

const th: React.CSSProperties = {
  textAlign: 'left',
  padding: '0.5rem 0.625rem',
  borderBottom: '1px solid var(--color-line-strong)',
  color: 'var(--color-ink-400)',
  fontSize: '0.6875rem',
  textTransform: 'uppercase',
  letterSpacing: '0.05em',
};

const td: React.CSSProperties = {
  padding: '0.4375rem 0.625rem',
  borderBottom: '1px solid var(--color-line)',
  color: 'var(--color-ink-200)',
};

export default CalculatorPageExtras;
