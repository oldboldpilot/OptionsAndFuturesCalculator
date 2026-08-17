import React from 'react';
import { Metadata } from 'next';
import StrategyWorkspace from '../../../components/StrategyWorkspace';
import { branding } from '@/config/branding';
import { StrategyStructuredData } from '@/components/StructuredData';
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
 * Per-strategy calculator page.
 *
 * The TOOL, and only the tool. The written guide that used to sit below this
 * workspace now lives at `/guides/<slug>`, which is where the advertising went
 * with it — these screens are outside `/guides/<slug>`, so they ship no ad code, as
 * mortgagefvcalculator.com's own calculator page does.
 *
 * That split is what makes both pages honest. One page trying to be a tool and
 * an article was neither: all twenty-six rendered identically apart from a
 * heading, which is what "ads on screens without publisher-content" names.
 * Now the intents are separate and cross-linked — "iron condor calculator"
 * lands here, "what is an iron condor" lands on the guide, and each links to
 * the other.
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

  const hasGuide = Boolean(getStrategyGuide(resolvedParams.strategy));

  return (
    <>
      <StrategyStructuredData
        slug={resolvedParams.strategy}
        name={`${strategyName}${/futures/i.test(strategyName) ? '' : isFutures ? ' Futures' : ' Options'} Calculator`}
        description={`Model the profit, loss and Greeks of ${/^[aeiou]/i.test(strategyName) ? 'an' : 'a'} ${strategyName} strategy on live market data.`}
      />
      <StrategyWorkspace
        heading={`${strategyName} Calculator`}
        // A real URL now, not the `#guide` anchor: the article moved to its own
        // page, so this is the crawlable link between the two intents rather
        // than a jump down the same document.
        guideHref={hasGuide ? `/guides/${resolvedParams.strategy}` : undefined}
      />
    </>
  );
}
