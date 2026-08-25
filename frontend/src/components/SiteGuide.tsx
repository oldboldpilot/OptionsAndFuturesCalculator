import React from 'react';
import Link from 'next/link';
import { STRATEGY_GUIDES } from '@/content/strategy-guides';

/**
 * The written material on the home page, and the index of every strategy page.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Two jobs, and the second one is easy to overlook. The obvious job is that the
 * home page carried no readable content at all — it rendered the workspace and
 * nothing else, which is what a policy reviewer sees when a tool has no prose
 * around it.
 *
 * The other job is LINKS. Until this existed, the twenty-six `/calculator/*`
 * pages were reachable only from `sitemap.xml`. A crawler following links from
 * the home page found none of them, and neither did a reader — the strategy
 * picker inside the workspace changes state in the client, it does not
 * navigate. A page that only a sitemap knows about is a page with no standing
 * in the site's own structure.
 */

/**
 * Grouped for the reader, not by the slug order.
 *
 * The grouping is declared here rather than derived from `outlook`, because
 * these are editorial buckets — "Income and hedging" spans bullish, bearish and
 * neutral structures and is still the category somebody browses by.
 */
const GROUPS: Array<{ title: string; blurb: string; slugs: string[] }> = [
  {
    title: 'Directional',
    blurb: 'A view on where the price goes, with the risk bounded by what you paid or by a strike you chose.',
    slugs: ['long-call', 'long-put', 'call-spread', 'put-spread', 'risk-reversal'],
  },
  {
    title: 'Income',
    blurb: 'Selling premium against a level you expect to hold. Positive time decay, and a loss that is larger than the credit.',
    slugs: ['bull-put-spread', 'bear-call-spread', 'covered-call', 'cash-secured-put', 'jade-lizard'],
  },
  {
    title: 'Neutral and range-bound',
    blurb: 'Structures that pay for the underlying staying put, with defined risk on both sides.',
    slugs: ['iron-condor', 'iron-butterfly', 'butterfly', 'condor'],
  },
  {
    title: 'Volatility',
    blurb: 'Direction-agnostic positions that profit from movement, or from the price of movement changing.',
    slugs: ['straddle', 'strangle', 'calendar-spread', 'diagonal-spread'],
  },
  {
    title: 'Hedging',
    blurb: 'Protecting a holding you intend to keep, and what that protection costs.',
    slugs: ['protective-put', 'collar'],
  },
  {
    title: 'Futures',
    blurb: 'Linear, margined exposure — and the spread structures that trade one contract against another.',
    slugs: [
      'futures-outright',
      'futures-spread',
      'futures-calendar-spread',
      'futures-intercommodity-spread',
      'futures-basis-arbitrage',
      'covered-futures-call',
    ],
  },
];

export function SiteGuide() {
  return (
    <div
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
      <h1 style={h1}>Pricing options and futures strategies on live market data</h1>

      <p style={p}>
        This is a modelling tool for multi-leg options and futures positions. You build a position
        leg by leg — or pick one of the twenty-six structures below — and it returns the payoff at
        expiry, the profit and loss across price and time, the full Greek profile and the
        probability of finishing profitable, priced from live option chains rather than from typed-in
        assumptions.
      </p>

      <h2 style={h2}>What the numbers come from</h2>
      <p style={p}>
        Quotes, strikes, implied volatilities and open interest come from a live option chain, and
        every chain carries the time it was fetched. The freshness chip beside it reads{' '}
        <strong>LIVE</strong> under a minute old and <strong>DELAYED</strong> with an as-of time
        after that — a distinction worth respecting, because a stale chain prices a position that no
        longer exists at those levels.
      </p>
      <p style={p}>
        Pricing runs on a native C++ engine rather than in the browser. European options are priced
        by Black–Scholes with closed-form Greeks; American and Bermudan exercise use binomial and
        trinomial trees; path-dependent structures use Monte Carlo. Futures carry, margin and basis
        are computed from the term structure of the contract you select, not from a flat rate.
      </p>

      <h2 style={h2}>How to read the outputs</h2>
      <ul style={list}>
        <li style={li}>
          <strong>Payoff at expiry</strong> is the arithmetic result — the straight-line diagram
          most people picture. It is exact, and it only applies on the expiry date.
        </li>
        <li style={li}>
          <strong>The P&amp;L matrix</strong> is the one that matters before then. It shows the
          position revalued across a grid of prices and dates, which is where time decay and
          volatility changes appear and where a payoff diagram tells you nothing.
        </li>
        <li style={li}>
          <strong>The probability curve</strong> is derived from the implied volatility of the chain
          you are pricing against. It is the market&rsquo;s distribution, not a forecast, and it is
          only as good as the assumption that returns are lognormal — which is least true in exactly
          the tails people use it to assess.
        </li>
        <li style={li}>
          <strong>The Greeks</strong> describe the position&rsquo;s sensitivity right now. Delta and
          gamma move fastest near a short strike close to expiry, which is where most defined-risk
          income strategies actually go wrong.
        </li>
      </ul>

      <h2 style={h2}>What it does not model</h2>
      <p style={p}>
        Commissions, exchange fees, financing and the bid/ask spread are excluded from every figure.
        On a four-legged position crossed twice, the spread is normally the largest cost in the
        trade and can exceed the credit collected. Early assignment on American-style short legs is
        not simulated, dividends are handled as a discrete yield rather than as dated payments, and
        margin figures are indicative — your broker&rsquo;s requirement is the one that matters.
      </p>

      <h2 style={{ ...h1, fontSize: 'clamp(1.375rem, 3vw, 1.75rem)', marginTop: '3rem' }}>
        Strategy guides
      </h2>
      <p style={p}>
        Each guide below explains one structure: how it is built, the closed-form maximum profit,
        maximum loss and breakeven, how it behaves before expiry, and the specific ways it goes
        wrong. Every one links through to the calculator, so you can price the structure on live
        quotes once you have read how it behaves.
      </p>

      {GROUPS.map((group) => (
        <section key={group.title} style={{ marginBottom: '2rem' }}>
          <h3 style={{ ...h2, marginTop: '1.5rem' }}>{group.title}</h3>
          <p style={{ ...p, marginBottom: '0.75rem', color: 'var(--color-ink-300)' }}>
            {group.blurb}
          </p>
          <ul
            style={{
              listStyle: 'none',
              margin: 0,
              padding: 0,
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fill, minmax(15rem, 1fr))',
              gap: '0.5rem',
            }}
          >
            {group.slugs.map((slug) => {
              const guide = STRATEGY_GUIDES[slug];
              // Skip rather than throw. A slug renamed in one place and not the
              // other should cost one link, not the whole build.
              if (!guide) return null;
              return (
                <li key={slug}>
                  <Link
                    // The GUIDE, not the calculator. This index is the Guides
                    // tab, so a card here opens the article; each article then
                    // carries its own link into the tool.
                    href={`/guides/${slug}`}
                    style={{
                      display: 'block',
                      padding: '0.625rem 0.75rem',
                      border: '1px solid var(--color-line)',
                      borderRadius: 'var(--radius-sm)',
                      background: 'var(--color-base-700)',
                      textDecoration: 'none',
                      color: 'var(--color-ink-100)',
                      fontSize: '0.875rem',
                      fontWeight: 600,
                    }}
                  >
                    {guide.name}
                    <span
                      style={{
                        display: 'block',
                        fontSize: '0.75rem',
                        fontWeight: 400,
                        color: 'var(--color-ink-400)',
                        marginTop: '0.125rem',
                      }}
                    >
                      {guide.outlook}
                    </span>
                  </Link>
                </li>
              );
            })}
          </ul>
        </section>
      ))}

      <p
        style={{
          margin: '2rem 0 0',
          paddingTop: '1.25rem',
          borderTop: '1px solid var(--color-line)',
          fontSize: '0.8125rem',
          color: 'var(--color-ink-400)',
        }}
      >
        <strong style={{ color: 'var(--color-ink-300)' }}>Educational only.</strong> Nothing on this
        site is a recommendation to trade. Options and futures carry substantial risk, and short and
        leveraged positions can lose more than the amount originally invested.
      </p>
    </div>
  );
}

const h1: React.CSSProperties = {
  fontFamily: 'var(--font-fraunces), Georgia, serif',
  fontSize: 'clamp(1.5rem, 3.5vw, 2rem)',
  lineHeight: 1.2,
  margin: '0 0 0.75rem',
  color: 'var(--color-ink-100)',
};

const h2: React.CSSProperties = {
  fontSize: '1.0625rem',
  fontWeight: 600,
  color: 'var(--color-ink-100)',
  margin: '2rem 0 0.625rem',
};

const p: React.CSSProperties = { margin: '0 0 1rem' };
const list: React.CSSProperties = { margin: '0 0 1rem', paddingLeft: '1.125rem' };
const li: React.CSSProperties = { marginBottom: '0.625rem' };

export default SiteGuide;
