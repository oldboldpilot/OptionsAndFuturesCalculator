import React from 'react';
import type { Metadata } from 'next';
import SiteGuide from '@/components/SiteGuide';
import { branding } from '@/config/branding';

/**
 * The Guides tab.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The index for the 26 articles, which are the only screens on this site that
 * carry Google advertising — the point of separating them from the calculator
 * being that they are the ones with publisher content on them.
 *
 * THIS PAGE CARRIES NO ADS, deliberately, and it is one segment above the
 * layout that ships the loader (`guides/[strategy]/layout.tsx`) precisely so
 * that stays true. It is a directory of 26 links, and the same policy that
 * flagged this site names "screens used for alerts, navigation or other
 * behavioral purposes" alongside screens without content. The ~300 words of
 * introduction above the directory do not change what the page is FOR.
 *
 * One page of 27 given up, on an account that has already been flagged once.
 */
export const metadata: Metadata = {
  title: 'Options & Futures Strategy Guides',
  description:
    'How twenty-six options and futures strategies work: construction, maximum profit and loss, breakeven, the Greeks, and the specific ways each one goes wrong.',
  alternates: { canonical: `${branding.canonicalUrl}/guides` },
  openGraph: {
    title: `Strategy Guides | ${branding.appName}`,
    description:
      'Construction, payoff identities, Greeks and failure modes for twenty-six options and futures strategies.',
    url: `${branding.canonicalUrl}/guides`,
    siteName: branding.companyName,
    type: 'website',
    images: [branding.ogImageUrl],
  },
  twitter: {
    card: 'summary_large_image',
    title: `Strategy Guides | ${branding.appName}`,
    description:
      'Construction, payoff identities, Greeks and failure modes for twenty-six options and futures strategies.',
    images: [branding.ogImageUrl],
  },
};

export default function GuidesIndexPage() {
  return <SiteGuide />;
}
