import React from 'react';
import { Metadata } from 'next';
import StrategyWorkspace from '../../../components/StrategyWorkspace';
import { branding } from '@/config/branding';
import { StrategyStructuredData } from '@/components/StructuredData';
import { getStrategyGuide } from '@/content/strategy-guides';
import { getCalculatorPageCopy } from '@/content/calculator-pages';
import { CalculatorPageExtras } from '@/components/CalculatorPageExtras';
import { pageTitle } from '@/lib/seo-title';

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
  const guide = getStrategyGuide(slug);
  const strategyName =
    guide?.name ??
    slug
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

  // AUTHORED per strategy, with the derived strings kept only as the fallback
  // for a slug that has no copy yet.
  //
  // The derivation is the defect. It produces one sentence with a name
  // substituted into it, which reads perfectly well on a single page and is
  // twenty-six near-identical pages to a crawler reading all of them -- median
  // 6-gram similarity 0.978, every page self-canonical, every page in the
  // sitemap. Search Console reported it as duplicate content.
  const copy = getCalculatorPageCopy(slug);

  const authoredTitle = copy?.title ?? `${strategyName} ${instrument}Calculator & Profit Visualizer`;
  const title = pageTitle(authoredTitle);
  const description =
    copy?.description ??
    `Calculate maximum profit, loss, probability of profit and the full Greek profile for ${article} ${strategyName}, priced from live ${isFutures ? 'futures' : 'option chain'} quotes.`;
  const ogTitle = `${copy?.heading ?? `${strategyName} Calculator`} | ${branding.appName}`;
  const ogDescription =
    copy?.description ??
    `Model the P&L and probability distribution of ${article} ${strategyName} strategy.`;

  return {
    title,
    description,
    // Its OWN url. Inherited from the root layout, every one of these pages
    // declared the HOMEPAGE as its canonical -- telling Google that all 26 are
    // duplicates of `/` and that none should be indexed in its own right, which
    // defeats the entire purpose of having per-strategy pages.
    alternates: {
      canonical: `${branding.canonicalUrl}/calculator/${slug}`,
    },
    openGraph: {
      title: ogTitle,
      description: ogDescription,
      url: `${branding.canonicalUrl}/calculator/${slug}`,
      siteName: branding.companyName,
      type: 'website',
      // A STATIC image. This pointed at /api/og?strategy=..., a route handler --
      // and `output: "export"` disables API routes, so every share and every
      // crawl of these pages fetched a 404 for its preview image.
      images: [branding.ogImageUrl],
    },
    twitter: {
      card: 'summary_large_image',
      title: ogTitle,
      description: ogDescription,
      images: [branding.ogImageUrl],
    },
  };
}

/**
 * Per-strategy calculator page.
 *
 * The TOOL, plus one authored paragraph about the structure it prices. The
 * written guide that used to sit below this workspace now lives at
 * `/guides/<slug>`, which is where the advertising went with it — these screens
 * are outside `/guides/<slug>`, so they ship no ad code, as
 * mortgagefvcalculator.com's own calculator page does.
 *
 * The paragraph is not a partial undo of that split. The article is still on
 * the guide; what is here is 80-120 words saying which structure this screen
 * prices and what goes wrong with it, because a page that differs from its
 * twenty-five siblings only by a heading is one Google declines to index —
 * measured 2026-09-16 at a median 6-gram similarity of 0.978.
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
  const slug = resolvedParams.strategy;
  const guide = getStrategyGuide(slug);
  const strategyName =
    guide?.name ??
    slug
      .split('-')
      .map(word => word.charAt(0).toUpperCase() + word.slice(1))
      .join(' ');

  const isFutures =
    slug.startsWith('futures-') ||
    slug === 'covered-futures-call';

  const hasGuide = Boolean(guide);
  const copy = getCalculatorPageCopy(slug);

  return (
    <>
      <StrategyStructuredData
        slug={slug}
        name={`${strategyName}${/futures/i.test(strategyName) ? '' : isFutures ? ' Futures' : ' Options'} Calculator`}
        description={
          copy?.description ??
          `Model the profit, loss and Greeks of ${/^[aeiou]/i.test(strategyName) ? 'an' : 'a'} ${strategyName} strategy on live market data.`
        }
      />
      <StrategyWorkspace
        heading={copy?.heading ?? `${strategyName} Calculator`}
        // A real URL now, not the `#guide` anchor: the article moved to its own
        // page, so this is the crawlable link between the two intents rather
        // than a jump down the same document.
        guideHref={hasGuide ? `/guides/${slug}` : undefined}
      />

      {/*
        The one authored paragraph on this screen, and it is here for SEARCH
        rather than for advertising.

        These pages carry no Google ad code -- they sit outside `/guides/<slug>`,
        which is the only subtree that emits the loader -- so nothing here is
        content written to satisfy an ad policy. What it fixes is a different
        problem with the same root: all twenty-six of these pages rendered the
        same workspace with one heading substituted, each declaring itself
        canonical and each listed in the sitemap, so the site asked Google to
        index twenty-six copies of one page and Google said no.

        Below the workspace deliberately. The shell above is pinned to the
        viewport and every panel inside it competes for those pixels; a
        paragraph in the header would cost the strike ladder rows on every
        strategy. This sits in the ordinary page flow with the broker links and
        the footer, which already scroll.

        It is NOT the guide's opening paragraph. Reusing that text would trade
        duplication between calculator pages for duplication between a
        calculator page and its own guide -- the same defect with one more URL
        in it -- so each lede is written separately and `check-export.mjs`
        asserts no sentence of one appears in the other.
      */}
      {copy && (
        <section
          data-strategy-lede={slug}
          style={{
            maxWidth: '60rem',
            margin: '0 auto',
            padding: '1.5rem 1.25rem 0',
            fontSize: '0.8125rem',
            lineHeight: 1.7,
            color: 'var(--color-ink-300)',
          }}
        >
          <p style={{ margin: 0 }}>{copy.lede}</p>
          {hasGuide && (
            <p style={{ margin: '0.75rem 0 0' }}>
              <a
                href={`/guides/${slug}`}
                style={{ color: 'var(--color-accent)', textDecoration: 'none' }}
              >
                {`The full ${strategyName} guide, worked example and FAQs →`}
              </a>
            </p>
          )}
        </section>
      )}

      {/*
        Third-wave content: mechanics, at-a-glance table, the order ticket,
        a computed payoff grid and a comparison with related strategies. See
        `calculator-extras.ts` for why the lede alone was not enough — the
        26 pages still measured a pairwise median 6-gram similarity of 0.768
        against each other with only the lede shipped.
      */}
      <CalculatorPageExtras slug={slug} />
    </>
  );
}
