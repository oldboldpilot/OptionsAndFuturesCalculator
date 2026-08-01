import type { Metadata } from "next";
import { Fraunces, Inter, JetBrains_Mono } from "next/font/google";
import "./globals.css";

// The three faces mortgagefvcalculator.com sets, so the two products read as
// one family rather than two tools that happen to share an owner.
//
// Inter for interface text, JetBrains Mono for figures — a strike ladder and a
// P&L grid need fixed advance widths to line up column to column, which no
// proportional face gives. Fraunces is the display serif, and it earns its
// place by being used sparingly: headings and the spot price, nothing dense.
const inter = Inter({
  variable: "--font-inter",
  weight: ["400", "600", "700"],
  subsets: ["latin"],
  display: "swap",
});

const jetbrainsMono = JetBrains_Mono({
  variable: "--font-jetbrains-mono",
  weight: ["400", "500", "600"],
  subsets: ["latin"],
  display: "swap",
});

// Variable optical size, exactly as the source loads it: Fraunces adjusts its
// contrast and detail to the size it is set at, so one axis covers a page
// heading and a 17px price without either looking like the other scaled.
// `weight` is omitted rather than pinned to 600/700: next/font rejects an
// explicit weight list alongside `axes`, because naming axes is what selects
// the variable font in the first place. The full weight range ships and CSS
// picks from it, which is also how the source site loads this face.
const fraunces = Fraunces({
  variable: "--font-fraunces",
  axes: ["opsz"],
  subsets: ["latin"],
  display: "swap",
});

import { branding } from "@/config/branding";
import { SiteStructuredData } from "@/components/StructuredData";

export async function generateMetadata(): Promise<Metadata> {
  return {
    // Without this Next resolves relative metadata URLs against the BUILD host,
    // which shipped `og:image` as http://localhost:3000/og-image.png to
    // production -- an unreachable, non-HTTPS image on every share and every
    // crawl.
    metadataBase: new URL(branding.canonicalUrl),
    title: {
      template: `%s | ${branding.appName}`,
      default: branding.appName,
    },
    description: branding.description,
    keywords: ["Options", "Futures", "Calculator", "Quantitative Finance", "Stochastic Modeling", "SGEE", "Derivatives"],
    authors: [{ name: branding.companyName }],
    creator: branding.companyName,
    openGraph: {
      title: branding.appName,
      description: branding.description,
      url: branding.canonicalUrl,
      siteName: branding.companyName,
      images: [
        {
          url: branding.ogImageUrl,
          width: 1200,
          height: 630,
          alt: branding.appName,
        },
      ],
      locale: "en_US",
      type: "website",
    },
    twitter: {
      card: "summary_large_image",
      title: branding.appName,
      description: branding.description,
      creator: branding.twitterHandle,
      images: [branding.ogImageUrl],
    },
    robots: {
      index: true,
      follow: true,
      googleBot: {
        index: true,
        follow: true,
        "max-video-preview": -1,
        "max-image-preview": "large",
        "max-snippet": -1,
      },
    },
    alternates: {
      canonical: branding.canonicalUrl,
    },
  };
}

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={`${inter.variable} ${jetbrainsMono.variable} ${fraunces.variable}`}>
      <head>
        {/*
          Applies the saved theme before first paint. Without this the page
          renders in the default theme and then swaps, which on a full-bleed
          dark/light change is a jarring flash rather than a subtle one.
        */}
        <script
          dangerouslySetInnerHTML={{
            __html:
              "try{var t=localStorage.getItem('ofc-theme');"+
              /* One-time migration: the dark 'slate' theme was the default
                 before the light rework, so a returning browser still carries
                 it and never sees the new default. Clear that one stored
                 value once; a choice made after this point is respected. */
              "if(!localStorage.getItem('ofc-theme-v2')){if(t==='slate'){t=null;localStorage.removeItem('ofc-theme')}localStorage.setItem('ofc-theme-v2','1')}"+
              "document.documentElement.dataset.theme=t||'light'}catch(e){document.documentElement.dataset.theme='light'}",
          }}
        />
        {/*
          AdSense site verification.

          Google looks for this meta tag to confirm the publisher who claims
          this site is the one who owns the account. It is separate from the ad
          loader below -- the loader requests ads, this asserts ownership -- and
          it is the tag AdSense asks for when a site is stuck awaiting review.

          The publisher id is not a secret: it is public in ads.txt by design.
        */}
        <meta name="google-adsense-account" content="ca-pub-3669553016263703" />

        {/* What this site IS, in a vocabulary crawlers parse rather than infer. */}
        <SiteStructuredData />

        {/*
          Google AdSense Auto Ads.

          `async` and placed after the theme script on purpose: this is
          third-party code on the critical path of a page whose first job is to
          show live prices, and it must never be able to delay or block that.

          Auto Ads means Google chooses placements, so a unit can appear inside
          the workspace rather than only in the box AdSlot reserves. On a layout
          where the thing below an insertion point is a strike ladder with buy
          and sell buttons, a late-arriving ad can shift a control under the
          cursor. Auto Ads was chosen deliberately over a fixed unit; if that
          shifting becomes a problem, the fix is to turn Auto Ads off in the
          AdSense dashboard and wire a manual unit into AdSlot with its slot id,
          which needs no code change here beyond the unit tag.

          The publisher id is not a secret — it is public in ads.txt by design,
          which is the whole point of ads.txt.
        */}
        <script
          async
          src="https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=ca-pub-3669553016263703"
          crossOrigin="anonymous"
        />
      </head>
      <body>
        <div id="root-container">
          {children}
        </div>
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
      </body>
    </html>
  );
}
