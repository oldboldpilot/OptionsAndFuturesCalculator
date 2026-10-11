import Link from "next/link";
import SponsoredBrokers from "@/components/SponsoredBrokers";
import { getStrategyGuide } from "@/content/strategy-guides";

const CALCULATOR_GROUPS: Array<{ title: string; slugs: readonly string[] }> = [
  {
    title: "Options Strategy Calculators",
    slugs: [
      "long-call",
      "long-put",
      "call-spread",
      "put-spread",
      "bull-put-spread",
      "bear-call-spread",
      "straddle",
      "strangle",
      "iron-condor",
      "iron-butterfly",
      "butterfly",
      "condor",
      "collar",
      "covered-call",
      "cash-secured-put",
      "protective-put",
      "jade-lizard",
      "calendar-spread",
      "diagonal-spread",
      "risk-reversal",
    ],
  },
  {
    title: "Futures Strategy Calculators",
    slugs: [
      "futures-outright",
      "futures-spread",
      "futures-calendar-spread",
      "futures-intercommodity-spread",
      "covered-futures-call",
      "futures-basis-arbitrage",
    ],
  },
];

/**
 * The broker block and the site footer.
 *
 * These were in the ROOT layout, so every route that exists carried them -- the
 * embeddable widget included, which is iframed into other people's sites and was
 * serving this site's broker affiliate links, a 26-link directory of its own
 * pages and a page-long disclaimer inside a third party's page (1,987px tall for
 * a calculator that is meant to be a few hundred). The widget is the one route
 * that is not this site's chrome, so the chrome moved to the `(site)` route
 * group's layout and the widget sits outside it.
 *
 * A server component with no props: it is the same markup everywhere it
 * renders, which is every page except the widget. The 404 renders it too,
 * because it lives outside the group and a dead end should still have the
 * directory.
 */
export default function SiteFooter() {
  return (
    <>
      {/*
        Broker links, served by this site rather than by Google.

        Above the multiplex deliberately. This module always renders; the
        AdSense unit below it renders only once Google approves the site and
        has something to fill it with. Putting the reliable block first means
        the page never opens with an empty reserved box as its first thing
        after the workspace.
      */}
      <div style={{ maxWidth: '78rem', margin: '0 auto', padding: '1.25rem 1.25rem 0' }}>
        <SponsoredBrokers />
      </div>
      {/*
        The multiplex unit used to sit here, in the SHARED layout, which meant
        it rendered on every screen the site serves — including the 404, whose
        arbitrary pathname defeated the only check that suppressed it. It now
        lives in `app/(site)/guides/[strategy]/layout.tsx` beside the loader that
        fills it, so the unit and its ad code cannot be shipped to different
        sets of pages.
      */}

      {/*
        Site-wide footer.

        Present for a specific reason: /privacy and /terms have to be
        REACHABLE, not merely to exist. A policy page nothing links to is not
        found by a crawler following links, and an advertising or payment
        reviewer checking whether this site discloses its cookie use will
        conclude it does not. Both were 404 until now.

        Kept out of the flow of the terminal itself so it cannot compete with
        live prices for attention.
      */}
      <footer
        style={{
          borderTop: '1px solid var(--color-line)',
          padding: '1.5rem 1.25rem 2rem',
          fontSize: '0.75rem',
          lineHeight: 1.6,
          color: 'var(--color-ink-400)',
        }}
      >
        {/*
          All Strategy Calculators directory.

          Links all 26 /calculator/<slug> pages sitewide so crawlers and
          visitors can navigate directly to any strategy calculator from any
          page, establishing crawl depth and link equity across the catalogue.
        */}
        <div
          style={{
            maxWidth: '60rem',
            margin: '0 auto 1.5rem',
            paddingBottom: '1.25rem',
            borderBottom: '1px solid var(--color-line)',
          }}
        >
          <div
            style={{
              fontSize: '0.8125rem',
              fontWeight: 600,
              color: 'var(--color-ink-200)',
              marginBottom: '0.75rem',
            }}
          >
            All Strategy Calculators
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem' }}>
            {CALCULATOR_GROUPS.map((group) => (
              <div key={group.title}>
                <div
                  style={{
                    fontSize: '0.6875rem',
                    fontWeight: 600,
                    textTransform: 'uppercase',
                    letterSpacing: '0.05em',
                    color: 'var(--color-ink-300)',
                    marginBottom: '0.375rem',
                  }}
                >
                  {group.title}
                </div>
                <div
                  style={{
                    display: 'flex',
                    flexWrap: 'wrap',
                    gap: '0.375rem 0.875rem',
                  }}
                >
                  {group.slugs.map((slug) => {
                    const guide = getStrategyGuide(slug);
                    return (
                      <Link
                        key={slug}
                        href={`/calculator/${slug}`}
                        style={{
                          color: 'inherit',
                          textDecoration: 'none',
                        }}
                      >
                        {guide?.name ?? slug}
                      </Link>
                    );
                  })}
                </div>
              </div>
            ))}
          </div>
        </div>

        <div
          style={{
            maxWidth: '60rem',
            margin: '0 auto',
            display: 'flex',
            flexWrap: 'wrap',
            gap: '0.75rem 1.25rem',
            alignItems: 'baseline',
            justifyContent: 'space-between',
          }}
        >
          <span>
            © {new Date().getFullYear()} <strong style={{ color: 'var(--color-ink-300)' }}>Knobugsoft LLC</strong>.
            All rights reserved.
          </span>
          <nav style={{ display: 'flex', flexWrap: 'wrap', gap: '1.25rem' }}>
            <a href="/privacy" style={{ color: 'inherit' }}>Privacy</a>
            <a href="/terms" style={{ color: 'inherit' }}>Terms</a>
            <a href="mailto:hello@optionsandfuturescalculator.com" style={{ color: 'inherit' }}>Contact</a>
          </nav>
        </div>

        {/*
          The disclaimer is the substantive part of this footer, not filler.

          This tool prices real instruments off live data and someone can act
          on a number it shows, so the limits have to be stated where they are
          read rather than only behind a link. The specific claims are the
          ones that matter for a derivatives calculator: it is not advice, the
          data may be stale, model outputs are estimates, and losses on short
          and leveraged positions are not bounded by what you put in.
        */}
        <div
          style={{
            maxWidth: '60rem',
            margin: '1rem auto 0',
            paddingTop: '1rem',
            borderTop: '1px solid var(--color-line)',
            color: 'var(--color-ink-500, var(--color-ink-400))',
          }}
        >
          <p style={{ margin: '0 0 0.5rem' }}>
            <strong style={{ color: 'var(--color-ink-300)' }}>Not investment advice.</strong>{' '}
            Options &amp; Futures Calculator is an educational and analytical tool. Nothing here is
            a recommendation, solicitation, or offer to buy or sell any security, futures contract
            or other instrument, and nothing here is tax, legal or accounting advice.
          </p>
          <p style={{ margin: '0 0 0.5rem' }}>
            <strong style={{ color: 'var(--color-ink-300)' }}>Trading risk.</strong> Options and
            futures carry substantial risk and are not suitable for every investor. Short and
            leveraged positions can lose more than the amount originally invested. Past
            performance and modelled results do not indicate future results.
          </p>
          <p style={{ margin: 0 }}>
            <strong style={{ color: 'var(--color-ink-300)' }}>Data and models.</strong> Market data
            is supplied by third parties, may be delayed or inaccurate, and is provided without
            warranty. Greeks, implied volatility and probabilities are model estimates that rest on
            assumptions which do not hold exactly in real markets. Figures shown exclude
            commissions, fees, financing and assignment risk. Knobugsoft LLC accepts no liability
            for any loss arising from use of this site. Verify independently before trading.
          </p>
        </div>
      </footer>
    </>
  );
}
