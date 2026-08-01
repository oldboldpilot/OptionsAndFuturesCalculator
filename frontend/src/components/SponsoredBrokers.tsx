import React from 'react';
import { BROKER_PARTNERS, HAS_PAID_PARTNER } from '@/config/affiliates';

/**
 * Broker links, served by this site rather than by an ad network.
 *
 * The reason this exists at all: mortgagefvcalculator.com, which runs on the
 * same AdSense publisher id as this site, renders ZERO AdSense units -- no
 * <ins>, no ad iframes -- and monetises entirely through hand-built sponsored
 * modules like this one. Its visible "ads" are four lender links with
 * rel="sponsored" and a commission disclosure. That module needs no approval,
 * no review queue and no fill rate: it is server-rendered HTML that appears for
 * every visitor on first paint. AdSense here remains wired and is orthogonal.
 *
 * Two properties this has and an ad unit does not, both of which matter on a
 * page that prices real positions:
 *
 *   It cannot reflow. The markup is static and sized by its own content, so it
 *   never grows on arrival and shoves a strike ladder under the cursor.
 *
 *   It cannot serve something unrelated. An ad network will happily put a
 *   crypto casino next to an iron condor. This list is four brokers chosen for
 *   this audience, and nothing else can appear in it.
 */
export function SponsoredBrokers() {
  return (
    <aside
      aria-label={HAS_PAID_PARTNER ? 'Sponsored broker links' : 'Broker links'}
      style={{
        border: '1px solid var(--color-line)',
        borderRadius: 'var(--radius-sm)',
        background: 'var(--color-base-700)',
        padding: '1rem',
      }}
    >
      <div
        style={{
          display: 'flex',
          alignItems: 'baseline',
          justifyContent: 'space-between',
          gap: '0.75rem',
          marginBottom: '0.25rem',
        }}
      >
        <h2 style={{ fontSize: 'var(--text-sm)', fontWeight: 600, margin: 0 }}>
          Where to trade these
        </h2>
        {/*
          The label tracks reality. It says "Sponsored" only when a partner link
          actually pays, because calling an unpaid outbound link sponsored is a
          false claim about a commercial relationship -- and the FTC rules that
          require disclosure of paid links equally do not permit inventing one.
          Flipping a trackingId in config/affiliates.ts changes this by itself.
        */}
        <span
          style={{
            fontSize: 'var(--text-2xs)',
            letterSpacing: '0.08em',
            textTransform: 'uppercase',
            color: 'var(--color-ink-400)',
          }}
        >
          {HAS_PAID_PARTNER ? 'Sponsored' : 'Brokers'}
        </span>
      </div>

      {/*
        Disclosure ABOVE the links, not in a footer. "Clear and conspicuous"
        under 16 CFR Part 255 means the reader meets it before the link, not
        after -- a disclosure below the thing it qualifies has already failed.
        The second sentence is the one that keeps a calculator from reading as
        investment advice.
      */}
      <p
        style={{
          fontSize: 'var(--text-2xs)',
          color: 'var(--color-ink-400)',
          margin: '0 0 0.75rem',
          lineHeight: 1.5,
        }}
      >
        {HAS_PAID_PARTNER
          ? 'We may earn a commission if you open an account through these links, at no cost to you. '
          : ''}
        This is not a recommendation and not financial, investment or tax advice.
        Options and futures carry risk of loss.
      </p>

      <div
        style={{
          display: 'grid',
          gap: '0.5rem',
          gridTemplateColumns: 'repeat(auto-fit, minmax(190px, 1fr))',
        }}
      >
        {BROKER_PARTNERS.map((b) => (
          <a
            key={b.name}
            href={b.url}
            target="_blank"
            // rel="sponsored" is the correct value for a paid link and tells
            // Google not to pass ranking signal through it. "nofollow" is the
            // honest value when nothing is paid: still no signal, no claim of
            // payment. noopener/noreferrer are unconditional -- target="_blank"
            // without noopener hands the destination a handle on this window.
            rel={`${b.trackingId ? 'sponsored' : 'nofollow'} noopener noreferrer`}
            style={{
              display: 'flex',
              flexDirection: 'column',
              gap: '0.25rem',
              padding: '0.625rem 0.75rem',
              border: '1px solid var(--color-line)',
              borderRadius: 'var(--radius-sm)',
              textDecoration: 'none',
              color: 'inherit',
            }}
          >
            <span style={{ fontSize: 'var(--text-sm)', fontWeight: 600 }}>{b.name}</span>
            <span
              style={{
                fontSize: 'var(--text-2xs)',
                color: 'var(--color-ink-400)',
                lineHeight: 1.45,
              }}
            >
              {b.blurb}
            </span>
            <span style={{ fontSize: 'var(--text-2xs)', marginTop: '0.125rem' }}>
              {b.cta} &rarr;
            </span>
          </a>
        ))}
      </div>
    </aside>
  );
}

export default SponsoredBrokers;
