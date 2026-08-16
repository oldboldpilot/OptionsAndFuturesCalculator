import React from 'react';
import { Metadata } from 'next';
import StrategyWorkspace from '../../../components/StrategyWorkspace';
import AdSlot from '@/components/AdSlot';
import { branding } from '@/config/branding';
import { StrategyStructuredData, FaqStructuredData } from '@/components/StructuredData';
import StrategyGuide from '@/components/StrategyGuide';
import { getStrategyGuide } from '@/content/strategy-guides';

// Shared with the sitemap, so the pages exported and the pages advertised to
// crawlers cannot drift apart.
import { STRATEGY_SLUGS as STRATEGIES } from '@/config/strategies';

interface Props {
  params: Promise<{ strategy: string }>;
}

// Generate Static Routes for each strategy at build time
export function generateStaticParams() {
  return STRATEGIES.map((strategy) => ({
    strategy: strategy,
  }));
}

// Dynamically generate SEO Metadata for each strategy page
export async function generateMetadata({ params }: Props): Promise<Metadata> {
  const resolvedParams = await params;
  const slug = resolvedParams.strategy;
  const strategyName = slug
    .split('-')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');

  // "for a Iron Condor" appeared verbatim in search results. The article has to
  // agree with the sound of the next word, and these names are fixed and known.
  const article = /^[aeiou]/i.test(strategyName) ? 'an' : 'a';

  // Futures strategies were all described as "Options Calculator", which is
  // both wrong and the single most important phrase on the page for matching
  // what someone typed.
  const isFutures = slug.startsWith('futures-') || slug === 'covered-futures-call';
  // Most futures slugs already carry the word, and "Futures Outright Futures
  // Calculator" is what naive concatenation produces. Only add the instrument
  // where the name does not already say it.
  const instrument = /futures/i.test(strategyName) ? '' : isFutures ? 'Futures ' : 'Options ';

  return {
    title: `${strategyName} ${instrument}Calculator & Profit Visualizer`,
    description: `Calculate maximum profit, loss, probability of profit and the full Greek profile for ${article} ${strategyName}, priced from live ${isFutures ? 'futures' : 'option chain'} quotes.`,
    // Its OWN url. Inherited from the root layout, every one of these pages
    // declared the HOMEPAGE as its canonical -- telling Google that all 26 are
    // duplicates of `/` and that none should be indexed in its own right, which
    // defeats the entire purpose of having per-strategy pages.
    alternates: {
      canonical: `${branding.canonicalUrl}/calculator/${slug}`,
    },
    openGraph: {
      title: `${strategyName} Calculator | ${branding.appName}`,
      description: `Model the P&L and probability distribution of ${article} ${strategyName} strategy.`,
      url: `${branding.canonicalUrl}/calculator/${slug}`,
      // A STATIC image. This pointed at /api/og?strategy=..., a route handler --
      // and `output: "export"` disables API routes, so every share and every
      // crawl of these pages fetched a 404 for its preview image.
      images: [branding.ogImageUrl],
    },
  };
}

/**
 * Per-strategy landing page.
 *
 * Shares the workspace with the home route so there is exactly one calculator
 * UI to maintain. The previous version rendered a separate glassmorphism
 * dashboard whose probability panel was hardcoded to `mean={100}
 * stdDev={15}` — a distribution for an instrument that does not exist.
 */
export default async function StrategyCalculatorPage({ params }: Props) {
  const resolvedParams = await params;
  const strategyName = resolvedParams.strategy
    .split('-')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');

  const isFutures =
    resolvedParams.strategy.startsWith('futures-') ||
    resolvedParams.strategy === 'covered-futures-call';

  // The written guide for this structure. Every slug in STRATEGY_SLUGS has one
  // and a build-time check enforces that, but this stays a lookup rather than a
  // non-null assertion: a slug added to the list without a guide should render
  // a page with no article, not throw during the static export and take the
  // whole build down.
  const guide = getStrategyGuide(resolvedParams.strategy);

  return (
    <>
      <StrategyStructuredData
        slug={resolvedParams.strategy}
        name={`${strategyName}${/futures/i.test(strategyName) ? '' : isFutures ? ' Futures' : ' Options'} Calculator`}
        description={`Model the profit, loss and Greeks of ${/^[aeiou]/i.test(strategyName) ? 'an' : 'a'} ${strategyName} strategy on live market data.`}
      />
      {guide && <FaqStructuredData faqs={guide.faqs} />}
      <StrategyWorkspace
        heading={`${strategyName} Calculator`}
        // The workspace is pinned to the viewport height, so the article below
        // it starts one full screen down. Without a link nothing on the first
        // screen says the page continues, and the content this page exists to
        // carry would go unread -- which is the same outcome as not having it.
        guideHref={guide ? '#guide' : undefined}
      />

      {guide && <StrategyGuide guide={guide} />}
      {/*
        In-article unit, on the strategy pages rather than in the root layout.

        These are the pages search sends people to, and they are the only ones
        that read as an article about one subject. The home page already carries
        the multiplex grid; stacking a second manual unit there would put two ad
        blocks in a row under the workspace.

        Below the workspace, never above it: this format sizes itself on arrival,
        and anything that changes height above a strike ladder moves the buy and
        sell buttons under the cursor.
      */}
      <div style={{ maxWidth: '78rem', margin: '0 auto', padding: '0 1rem 2rem' }}>
        <AdSlot size="in-article" label="Sponsored" />
      </div>
    </>
  );
}
