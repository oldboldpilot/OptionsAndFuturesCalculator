import type { Metadata } from "next";
import { Open_Sans, IBM_Plex_Mono } from "next/font/google";
import "./globals.css";

// Open Sans is what mortgagecalculator.org sets, and the reference palette was
// sampled from the same page. Plex Mono carries figures only: a strike ladder
// and a P&L grid need fixed advance widths to line up column to column, which
// no proportional face gives.
const openSans = Open_Sans({
  variable: "--font-open-sans",
  subsets: ["latin"],
  display: "swap",
});

const plexMono = IBM_Plex_Mono({
  variable: "--font-plex-mono",
  weight: ["400", "500", "600"],
  subsets: ["latin"],
  display: "swap",
});

import { branding } from "@/config/branding";

export async function generateMetadata(): Promise<Metadata> {
  return {
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
    <html lang="en" className={`${openSans.variable} ${plexMono.variable}`}>
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
      </body>
    </html>
  );
}
