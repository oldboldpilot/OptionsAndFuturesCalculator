import { MetadataRoute } from 'next';
import { branding } from '@/config/branding';

export const dynamic = 'force-static';

export default function robots(): MetadataRoute.Robots {
  return {
    rules: {
      userAgent: '*',
      allow: '/',
      // `/_next/` must NOT be disallowed. Every stylesheet and script chunk of
      // this static export lives under /_next/static/, and Google's crawlers
      // (Googlebot and the AdSense reviewer, Mediapartners-Google) obey
      // robots.txt for page resources. Blocking it made the site render for
      // review as bare unstyled HTML with no working application — indexing
      // and the AdSense review both judge the page as users see it, and they
      // cannot when the assets are forbidden.
      disallow: ['/api/', '/admin/'],
    },
    sitemap: `${branding.canonicalUrl}/sitemap.xml`,
  };
}
