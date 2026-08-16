import React from 'react';
import type { Metadata } from 'next';
import Link from 'next/link';
import StrategyGuide from '@/components/StrategyGuide';
import AdSlot from '@/components/AdSlot';
import { branding } from '@/config/branding';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { getStrategyGuide } from '@/content/strategy-guides';
import { StrategyStructuredData, FaqStructuredData } from '@/components/StructuredData';

/**
 * The written guide for one strategy.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Split out of `/calculator/<slug>`, which now carries the tool alone. The two
 * pages serve two different intents and are cross-linked in both directions:
 * "iron condor calculator" wants the ladder, "what is an iron condor" wants
 * this. Previously one page tried to be both and was neither — twenty-six
 * calculator pages rendering identically except for a heading.
 *
 * This is an ad-serving screen, which is allowed precisely because it is the
 * one with the content on it.
 */

interface Props {
  params: Promise<{ strategy: string }>;
}

export function generateStaticParams() {
  return STRATEGY_SLUGS.map((strategy) => ({ strategy }));
}

export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const { strategy: slug } = await params;
  const guide = getStrategyGuide(slug);
  const name =
    guide?.name ??
    slug.split('-').map((w) => w.charAt(0).toUpperCase() + w.slice(1)).join(' ');

  return {
    title: `${name}: Payoff, Breakeven and Greeks Explained`,
    description:
      guide
        ? `${name} explained: how it is built, maximum profit and loss, breakeven, the Greeks before expiry, and what goes wrong. ${guide.outlook}.`
        : `How the ${name} strategy works.`,
    alternates: { canonical: `${branding.canonicalUrl}/guides/${slug}` },
    openGraph: {
      title: `${name} explained | ${branding.appName}`,
      description: guide?.lede.slice(0, 200) ?? `How the ${name} strategy works.`,
      url: `${branding.canonicalUrl}/guides/${slug}`,
      images: [branding.ogImageUrl],
    },
  };
}

export default async function StrategyGuidePage({ params }: Props) {
  const { strategy: slug } = await params;
  const guide = getStrategyGuide(slug);

  if (!guide) {
    // A slug in STRATEGY_SLUGS with no guide should render a findable page that
    // points at the tool, not a build failure and not a dead end.
    return (
      <div style={{ maxWidth: '46rem', margin: '0 auto', padding: '3rem 1.5rem' }}>
        <p style={{ color: 'var(--color-ink-200)' }}>
          No written guide for this strategy yet.{' '}
          <Link href={`/calculator/${slug}`} style={{ color: 'var(--color-accent)' }}>
            Open it in the calculator
          </Link>
          .
        </p>
      </div>
    );
  }

  return (
    <>
      <StrategyStructuredData
        slug={slug}
        name={`${guide.name}: payoff, breakeven and Greeks`}
        description={guide.lede.slice(0, 200)}
      />
      <FaqStructuredData faqs={guide.faqs} />

      <StrategyGuide guide={guide} />

      {/*
        The link back to the tool, given real prominence rather than left in
        prose. Somebody who arrives here from a search for how the structure
        works very often wants to price one next, and this is the only route
        from the article to the ladder.
      */}
      <div style={{ maxWidth: '46rem', margin: '0 auto', padding: '0 1.5rem 1rem' }}>
        <Link
          href={`/calculator/${slug}`}
          style={{
            display: 'inline-block',
            padding: '0.625rem 1rem',
            borderRadius: 'var(--radius-sm)',
            background: 'var(--color-accent)',
            color: '#ffffff',
            textDecoration: 'none',
            fontWeight: 600,
            fontSize: '0.875rem',
          }}
        >
          Price a {guide.name} on live market data →
        </Link>
      </div>

      <div style={{ maxWidth: '46rem', margin: '0 auto', padding: '1.5rem 1.5rem 2rem' }}>
        <AdSlot size="in-article" label="Sponsored" />
      </div>
    </>
  );
}
