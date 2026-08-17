import Link from 'next/link';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { getStrategyGuide } from '@/content/strategy-guides';

/**
 * The 404 screen.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * There was no `not-found.tsx` at all until 2026-08-17, so the export shipped
 * Next's default — the words "404: This page could not be found" and nothing
 * else. That page then inherited the root layout, which at the time carried the
 * AdSense loader, an `enable_page_level_ads` push and a multiplex unit, and the
 * only thing suppressing them was a `location.pathname` denylist that a 404's
 * arbitrary pathname could never match.
 *
 * So every dead URL on this site served Google ads on thirteen words of error
 * text. That is the AdSense notice verbatim: a screen without content, used for
 * alerts.
 *
 * The ad code is gone from the shared layout — it now ships only from
 * `app/guides/[strategy]/layout.tsx` — so this page cannot carry ads no matter
 * what its pathname is. This file exists for the OTHER half of that finding:
 * the screen was also useless. A visitor who mistypes a URL, or follows a link
 * to a strategy page that was renamed, should land somewhere that gets them
 * where they were going.
 *
 * Deliberately NOT an ad-serving screen even though it now has real links on
 * it. An error page is navigational by definition, which is the third condition
 * the same policy names, and no amount of helpful content changes what the page
 * is for.
 */

export const metadata = {
  title: 'Page not found',
  // A 404 that gets indexed is a 404 that competes with the real pages. The
  // status code already says this to a well-behaved crawler; this says it to
  // the rest.
  robots: { index: false, follow: true },
};

/** A short, stable sample — the structures most people arrive looking for. */
const POPULAR = ['long-call', 'iron-condor', 'covered-call', 'straddle', 'futures-outright'] as const;

export default function NotFound() {
  const popular = POPULAR.filter((slug) => (STRATEGY_SLUGS as readonly string[]).includes(slug));

  return (
    <main
      style={{
        maxWidth: '48rem',
        margin: '0 auto',
        padding: '3rem 1.25rem 4rem',
        color: 'var(--color-ink-200)',
      }}
    >
      <p
        style={{
          fontSize: 'var(--text-2xs)',
          letterSpacing: '0.12em',
          textTransform: 'uppercase',
          color: 'var(--color-ink-400)',
        }}
      >
        404
      </p>
      <h1
        style={{
          fontFamily: 'var(--font-fraunces), Georgia, serif',
          fontSize: '1.75rem',
          fontWeight: 700,
          margin: '0.5rem 0 0.75rem',
        }}
      >
        That page isn&rsquo;t here
      </h1>
      <p style={{ lineHeight: 1.7, color: 'var(--color-ink-300)' }}>
        The address may be mistyped, or the page may have moved. Everything on
        this site is reachable from two places: the calculator, where you build a
        position leg by leg on live market data, and the guides, which explain
        each structure and its payoff.
      </p>

      <div style={{ display: 'flex', gap: '0.75rem', flexWrap: 'wrap', margin: '1.5rem 0' }}>
        <Link className="btn btn-primary" href="/" style={{ textDecoration: 'none' }}>
          Open the calculator
        </Link>
        <Link className="btn" href="/guides" style={{ textDecoration: 'none' }}>
          Browse the guides
        </Link>
      </div>

      <h2 style={{ fontSize: '1rem', fontWeight: 600, margin: '2rem 0 0.75rem' }}>
        Popular strategies
      </h2>
      <ul style={{ lineHeight: 1.9, paddingLeft: '1.1rem' }}>
        {popular.map((slug) => {
          const guide = getStrategyGuide(slug);
          return (
            <li key={slug}>
              <Link href={`/guides/${slug}`} style={{ color: 'var(--color-accent)' }}>
                {guide?.name ?? slug}
              </Link>
              {guide ? <span style={{ color: 'var(--color-ink-400)' }}> — {guide.outlook}</span> : null}
              {' · '}
              <Link href={`/calculator/${slug}`} style={{ color: 'var(--color-ink-300)' }}>
                price one
              </Link>
            </li>
          );
        })}
      </ul>
    </main>
  );
}
