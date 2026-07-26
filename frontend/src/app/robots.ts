import { MetadataRoute } from 'next';
import { branding } from '@/config/branding';

export const dynamic = 'force-static';

export default function robots(): MetadataRoute.Robots {
  return {
    rules: {
      userAgent: '*',
      allow: '/',
      disallow: ['/api/', '/admin/', '/_next/'],
    },
    sitemap: `${branding.canonicalUrl}/sitemap.xml`,
  };
}
