import React from 'react';
import type { Metadata } from 'next';
import SiteGuide from '@/components/SiteGuide';
import { branding } from '@/config/branding';

/**
 * The Guides tab.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This page and `/guides/<slug>` are the only screens on the site that carry
 * Google advertising, and that is the whole point of separating them from the
 * calculator: they are the ones with publisher content on them. The tool
 * screens are in NO_AD_ROUTES.
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
    images: [branding.ogImageUrl],
  },
};

export default function GuidesIndexPage() {
  return <SiteGuide />;
}
