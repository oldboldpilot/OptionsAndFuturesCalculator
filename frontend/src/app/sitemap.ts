import { MetadataRoute } from 'next';
import { branding } from '@/config/branding';
import { STRATEGY_SLUGS } from '@/config/strategies';

export const dynamic = 'force-static';

/**
 * Every page this site actually exports.
 *
 * The previous version listed three URLs on a DIFFERENT domain -- the root,
 * `/options` and `/futures` on sensen.io -- none of which this site serves and
 * two of which do not exist anywhere. A sitemap is a crawler's first
 * impression, and this one said the site was three broken pages belonging to
 * somebody else.
 *
 * Generated from the shared slug list rather than hand-written, so a strategy
 * added to the app cannot go missing here. That drift is invisible: the pages
 * build and work, search traffic simply never arrives on them.
 */
/*
 * No `lastModified` anywhere, deliberately.
 *
 * It used to be `new Date()` evaluated at build time, which stamped all 29
 * URLs with the instant of the build. Every deploy then told crawlers the
 * entire site had just changed -- including deploys that touched one component,
 * or only the backend. A lastmod that moves when the page did not is worse than
 * no lastmod: Google discounts the signal for the whole file once it looks
 * unreliable, so the churn costs the accurate entries too.
 *
 * mortgagefvcalculator.com, which is indexed and serving on this same AdSense
 * account, omits lastmod entirely for the same reason. Matching it.
 *
 * If per-page dates are wanted later they have to come from real edit times --
 * git's last-commit date for the file, not the clock at build.
 */
export default function sitemap(): MetadataRoute.Sitemap {
  const base = branding.canonicalUrl;

  return [
    {
      url: base,
      changeFrequency: 'weekly',
      priority: 1,
    },
    // The Guides index. Higher priority than any single guide because it is
    // the hub every one of them links back to.
    {
      url: `${base}/guides`,
      changeFrequency: 'weekly' as const,
      priority: 0.9,
    },
    // Two pages per strategy, deliberately, because they answer two different
    // searches: "iron condor calculator" wants the tool and "what is an iron
    // condor" wants the article. They were one page until 2026-08-16, which
    // meant twenty-six calculator screens rendering identically apart from a
    // heading. Both are listed and both are cross-linked.
    ...STRATEGY_SLUGS.flatMap((slug) => [
      {
        url: `${base}/calculator/${slug}`,
        changeFrequency: 'weekly' as const,
        priority: 0.8,
      },
      {
        url: `${base}/guides/${slug}`,
        changeFrequency: 'monthly' as const,
        priority: 0.7,
      },
    ]),
    {
      url: `${base}/privacy`,
      changeFrequency: 'yearly',
      priority: 0.3,
    },
    {
      url: `${base}/terms`,
      changeFrequency: 'yearly',
      priority: 0.3,
    },
  ];
}
