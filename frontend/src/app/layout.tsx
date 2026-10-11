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
import SiteNav from "@/components/SiteNav";

export async function generateMetadata(): Promise<Metadata> {
  return {
    // Without this Next resolves relative metadata URLs against the BUILD host,
    // which shipped `og:image` as http://localhost:3000/og-image.png to
    // production -- an unreachable, non-HTTPS image on every share and every
    // crawl.
    metadataBase: new URL(branding.canonicalUrl),
    title: branding.appName,
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
      // Spread, so an unset handle emits no `creator` key at all. Passing an
      // empty string would render <meta name="twitter:creator" content=""/>,
      // which is a malformed tag rather than an absent one.
      ...(branding.twitterHandle ? { creator: branding.twitterHandle } : {}),
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
          AdSense site VERIFICATION, and the only AdSense-related tag in this
          shared layout.

          It asserts who owns the site; it requests nothing. That separation is
          what lets the ad LOADER live in `app/(site)/guides/[strategy]/layout.tsx`
          alone — ownership is still stated on all fifty-nine exported pages,
          while ad code exists on twenty-six of them. Removing the loader from
          here therefore cannot put the site back into "we can't find the code".

          The publisher id is not a secret: it is public in ads.txt by design.
        */}
        <meta name="google-adsense-account" content="ca-pub-3669553016263703" />

        {/* What this site IS, in a vocabulary crawlers parse rather than infer. */}
        <SiteStructuredData />

        {/*
          NO AD LOADER HERE, AND THAT IS THE FIX. Do not restore one.

          This head carried `adsbygoogle.js`, an `enable_page_level_ads` push,
          and an inline guard that set `pauseAdRequests` when
          `location.pathname` matched a denylist. Every page on the site loaded
          Google's ad code and a runtime test decided whether it was allowed to
          fill.

          The 404 walked straight through that test. Its pathname is whatever
          the visitor typed, so it matched no denylist entry, and
          `/this-page-does-not-exist` served page-level Auto Ads plus the
          multiplex unit that used to sit in this file's <body> — on a screen
          whose own content is "404: This page could not be found". Measured
          live on 2026-08-17, a day after the first publisher-content fix was
          deployed and believed complete. AdSense's notice names exactly this:
          screens without content, and screens used for alerts.

          A denylist can only protect routes somebody enumerated. The error page
          is the one route nobody writes down, and it is the one a crawler
          probing dead URLs hits most.

          So the loader moved to the ONE subtree that carries articles, per
          Next's own guidance for loading a third-party script on a subset of
          routes (`node_modules/next/dist/docs/01-app/02-guides/scripts.md`,
          "Layout Scripts"). A page outside `/guides/<slug>` now has no ad code
          to suppress, which is a stronger statement than a suppressed one and
          needs no correct runtime behaviour to hold.
        */}
      </head>
      <body>
        {/*
          Site tabs, above everything. Slim by design: the calculator below is
          pinned to the viewport, so every pixel here comes out of the strike
          ladder. It declares its height as --nav-h and the workspace subtracts
          exactly that variable, so the two cannot drift apart.
        */}
        {/* Skip link: the first Tab stop on every page, visible only when it
            has focus. Before it, a keyboard user's first stop was the site
            name and the order ticket was the forty-fifth. */}
        <a className="skip-link" href="#main">
          Skip to main content
        </a>
        <SiteNav />
        {/*
          No <main> here, deliberately. Each page group owns it: `app/(site)/layout.tsx`
          for the site (with the footer beneath it, outside the landmark) and
          `app/widget/layout.tsx` for the embed (with no footer at all). A <main>
          in this file would have wrapped whichever of those nested inside it,
          footer included.
        */}
        {children}
      </body>
    </html>
  );
}
