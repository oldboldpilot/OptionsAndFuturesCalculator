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
export default function sitemap(): MetadataRoute.Sitemap {
  const now = new Date();
  const base = branding.canonicalUrl;

  return [
    {
      url: base,
      lastModified: now,
      changeFrequency: 'daily',
      priority: 1,
    },
    // The strategy pages are the substance of the site and the reason anyone
    // arrives from a search.
    ...STRATEGY_SLUGS.map((slug) => ({
      url: `${base}/calculator/${slug}`,
      lastModified: now,
      changeFrequency: 'weekly' as const,
      priority: 0.8,
    })),
    {
      url: `${base}/privacy`,
      lastModified: now,
      changeFrequency: 'yearly',
      priority: 0.3,
    },
    {
      url: `${base}/terms`,
      lastModified: now,
      changeFrequency: 'yearly',
      priority: 0.3,
    },
  ];
}
