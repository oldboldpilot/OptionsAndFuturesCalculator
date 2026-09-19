/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Tests for SEO title sizing and metadata length boundaries across all routes.
 *
 * THE DEFECT (measured on 2026-09-19):
 * `src/app/layout.tsx` set a Next.js title template appending
 * ` | Options & Futures Calculator` (31 characters) to every page title
 * unconditionally. 53 of the 59 exported HTML pages exceeded 70 characters,
 * the longest reaching 101 (`out/guides/futures-intercommodity-spread.html`).
 * Because Google truncates SERP titles past roughly 70 characters, the
 * distinguishing strategy-specific part was cut off across most of the site.
 * In addition, 5 meta descriptions exceeded 160 characters (longest was 193).
 *
 * WHY THIS TEST MEASURES THE RENDERED TITLE:
 * `src/content/calculator-pages.test.ts` formerly checked authored strings
 * against an arbitrary limit (62) and mentioned the 31-character suffix only
 * in a comment — measuring a string that never actually shipped. By testing
 * the rendered title produced by `pageTitle()`, we verify the exact string
 * emitted to search engines and assert that the brand suffix gives way whenever
 * space is needed for the specific strategy name.
 */
import { describe, it, expect } from 'vitest';
import { STRATEGY_SLUGS } from '@/config/strategies';
import { getCalculatorPageCopy } from '@/content/calculator-pages';
import { getStrategyGuide } from '@/content/strategy-guides';
import { branding } from '@/config/branding';
import { pageTitle, TITLE_MAX, DESCRIPTION_MAX, BRAND_SUFFIX } from '@/lib/seo-title';

describe('pageTitle helper', () => {
  it('keeps the brand suffix when the combined title fits within TITLE_MAX', () => {
    // 14 + 31 = 45 <= 70
    const rendered = pageTitle('Privacy Policy');
    expect(rendered).toBe('Privacy Policy | Options & Futures Calculator');
    expect(rendered.length).toBeLessThanOrEqual(TITLE_MAX);
  });

  it('drops the brand suffix when the combined title exceeds TITLE_MAX', () => {
    // 40 + 31 = 71 > 70
    const authored = 'A'.repeat(40);
    const rendered = pageTitle(authored);
    expect(rendered).toBe(authored);
    expect(rendered).not.toContain(BRAND_SUFFIX);
  });

  it('handles the exact boundary at 70 characters', () => {
    // 39 + 31 = 70: exactly at boundary -> suffix kept
    const authored39 = 'A'.repeat(39);
    const rendered39 = pageTitle(authored39);
    expect(rendered39.length).toBe(70);
    expect(rendered39).toBe(`${authored39}${BRAND_SUFFIX}`);

    // 40 + 31 = 71: one over boundary -> suffix dropped, bare title returned
    const authored40 = 'A'.repeat(40);
    const rendered40 = pageTitle(authored40);
    expect(rendered40.length).toBe(40);
    expect(rendered40).toBe(authored40);

    // 70-character authored title: bare title returned at boundary
    const authored70 = 'A'.repeat(70);
    const rendered70 = pageTitle(authored70);
    expect(rendered70.length).toBe(70);
    expect(rendered70).toBe(authored70);

    // 71-character authored title: returned unchanged (never truncated mid-word by helper)
    const authored71 = 'A'.repeat(71);
    const rendered71 = pageTitle(authored71);
    expect(rendered71.length).toBe(71);
    expect(rendered71).toBe(authored71);
  });
});

interface MeasuredPage {
  path: string;
  title: string;
  description: string;
}

function getSitePages(): MeasuredPage[] {
  const pages: MeasuredPage[] = [];

  // 1. All calculator pages derived from the source of truth
  for (const slug of STRATEGY_SLUGS) {
    const copy = getCalculatorPageCopy(slug);
    const guide = getStrategyGuide(slug);
    const strategyName =
      guide?.name ??
      slug
        .split('-')
        .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
        .join(' ');
    const isFutures = slug.startsWith('futures-') || slug === 'covered-futures-call';
    const instrument = /futures/i.test(strategyName) ? '' : isFutures ? 'Futures ' : 'Options ';
    const article = /^[aeiou]/i.test(strategyName) ? 'an' : 'a';

    const authoredTitle = copy?.title ?? `${strategyName} ${instrument}Calculator & Profit Visualizer`;
    const description =
      copy?.description ??
      `Calculate maximum profit, loss, probability of profit and the full Greek profile for ${article} ${strategyName}, priced from live ${isFutures ? 'futures' : 'option chain'} quotes.`;

    pages.push({
      path: `/calculator/${slug}`,
      title: pageTitle(authoredTitle),
      description,
    });
  }

  // 2. All guide pages derived from the source of truth
  for (const slug of STRATEGY_SLUGS) {
    const guide = getStrategyGuide(slug);
    const name =
      guide?.name ??
      slug
        .split('-')
        .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
        .join(' ');

    const authoredTitle = `${name}: Payoff, Breakeven and Greeks Explained`;
    const description = guide
      ? `${name} explained: how it is built, maximum profit and loss, breakeven, Greeks, and what goes wrong. ${guide.outlook}.`
      : `How the ${name} strategy works.`;

    pages.push({
      path: `/guides/${slug}`,
      title: pageTitle(authoredTitle),
      description,
    });
  }

  // 3. Static routes
  pages.push(
    {
      path: '/',
      title: pageTitle('Options & Futures Profit Calculator - Free Payoff & Greeks'),
      description:
        'Free options profit calculator & futures analysis tool. Visualize multi-leg payoff diagrams, calculate real-time Greeks, profit probabilities & P&L grids.',
    },
    {
      path: '/guides',
      title: pageTitle('Options & Futures Strategy Guides'),
      description:
        'How twenty-six options and futures strategies work: construction, maximum profit and loss, breakeven, the Greeks, and the specific ways each one goes wrong.',
    },
    {
      path: '/privacy',
      title: pageTitle('Privacy Policy'),
      description: `How ${branding.appName} handles your data, cookies and advertising.`,
    },
    {
      path: '/terms',
      title: pageTitle('Terms of Use'),
      description: `Terms of use for ${branding.appName}, including the financial disclaimer.`,
    },
    {
      path: '/widget',
      title: pageTitle('Embeddable widget'),
      description: branding.description,
    },
    {
      path: '/not-found',
      title: pageTitle('Page not found'),
      description: branding.description,
    },
  );

  return pages;
}

describe('rendered titles and descriptions across all site pages', () => {
  const pages = getSitePages();

  it('measures a non-vacuous sweep of pages (> 25 positive control)', () => {
    // 26 calculator pages + 26 guide pages + 6 static routes = 58 pages
    expect(pages.length).toBeGreaterThan(25);
  });

  it('keeps rendered titles between 10 and 70 characters on every page', () => {
    for (const page of pages) {
      expect(
        page.title.length,
        `${page.path} title exceeds ${TITLE_MAX} characters: "${page.title}" (${page.title.length})`,
      ).toBeLessThanOrEqual(TITLE_MAX);

      expect(
        page.title.length,
        `${page.path} title is under 10 characters: "${page.title}" (${page.title.length})`,
      ).toBeGreaterThanOrEqual(10);
    }
  });

  it('keeps descriptions between 50 and 160 characters on every page', () => {
    for (const page of pages) {
      expect(
        page.description.length,
        `${page.path} description exceeds ${DESCRIPTION_MAX} characters: "${page.description}" (${page.description.length})`,
      ).toBeLessThanOrEqual(DESCRIPTION_MAX);

      expect(
        page.description.length,
        `${page.path} description is under 50 characters: "${page.description}" (${page.description.length})`,
      ).toBeGreaterThanOrEqual(50);
    }
  });
});
