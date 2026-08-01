import React from 'react';
import { Metadata } from 'next';
import StrategyWorkspace from '../../../components/StrategyWorkspace';

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
  const strategyName = resolvedParams.strategy
    .split('-')
    .map(word => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');

  return {
    title: `${strategyName} Options Calculator & Profit Visualizer`,
    description: `Calculate maximum profit, loss, probability of profit and the full Greek profile for a ${strategyName}, priced from live option chain quotes.`,
    openGraph: {
      title: `${strategyName} Calculator | Options & Futures`,
      description: `Model the P&L and probability distribution of a ${strategyName} strategy.`,
      images: [`/api/og?strategy=${resolvedParams.strategy}`],
    }
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

  return <StrategyWorkspace heading={`${strategyName} Calculator`} />;
}
