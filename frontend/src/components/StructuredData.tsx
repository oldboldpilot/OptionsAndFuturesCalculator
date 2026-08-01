import React from 'react';
import { branding } from '@/config/branding';

/**
 * JSON-LD describing what this site is, for search engines.
 *
 * Titles and descriptions say what a page is ABOUT. Structured data says what
 * it IS, in a vocabulary Google parses rather than infers -- which is what
 * makes a result eligible for richer presentation instead of a plain blue link.
 * The site had none at all.
 *
 * Emitted as a script tag rather than through `next/script` because it is inert
 * data: it must be in the HTML the crawler receives on first fetch, not
 * injected once React hydrates. This is a static export served from a CDN, so
 * anything a crawler needs has to be in the document itself.
 */
/**
 * Serialises JSON-LD for embedding in a <script> element.
 *
 * `JSON.stringify` does NOT escape `<`, so a value containing the literal text
 * `</script>` would terminate the element early and everything after it would
 * be parsed as markup. Nothing here is user-supplied today -- these values come
 * from our own constants and a fixed slug list -- but that is a property of the
 * current callers, not of this function, and it is the kind of assumption that
 * quietly stops holding when someone passes a description through later.
 *
 * Escaping to \u003c keeps the JSON semantically identical (a JSON parser
 * resolves the escape) while making it impossible to close the tag.
 */
function jsonLd(data: unknown): string {
  return JSON.stringify(data)
    .replace(/</g, '\\u003c')
    .replace(/>/g, '\\u003e')
    .replace(/&/g, '\\u0026');
}

export function SiteStructuredData() {
  const data = {
    '@context': 'https://schema.org',
    '@graph': [
      {
        '@type': 'WebApplication',
        '@id': `${branding.canonicalUrl}/#app`,
        name: branding.appName,
        url: branding.canonicalUrl,
        description: branding.description,
        applicationCategory: 'FinanceApplication',
        operatingSystem: 'Any',
        browserRequirements: 'Requires JavaScript',
        // Stated because it is true and because it is what someone comparing
        // tools wants to know before clicking: the basic calculator is free.
        offers: [
          {
            '@type': 'Offer',
            name: 'Free',
            price: '0',
            priceCurrency: 'USD',
            description: 'Single-leg call and put calculators.',
          },
          {
            '@type': 'Offer',
            name: 'Pro',
            price: '9.99',
            priceCurrency: 'USD',
            description: 'Multi-leg strategies: spreads, straddles, condors, butterflies and futures spreads.',
          },
        ],
        featureList: [
          'Options payoff diagrams',
          'Greeks (delta, gamma, theta, vega, rho)',
          'Implied volatility',
          'Probability of profit',
          'Futures term structure',
          'Multi-leg strategy modelling',
        ],
      },
      {
        '@type': 'WebSite',
        '@id': `${branding.canonicalUrl}/#website`,
        url: branding.canonicalUrl,
        name: branding.appName,
        description: branding.description,
        publisher: { '@id': `${branding.canonicalUrl}/#org` },
      },
      {
        '@type': 'Organization',
        '@id': `${branding.canonicalUrl}/#org`,
        name: branding.companyName,
        url: branding.canonicalUrl,
        logo: `${branding.canonicalUrl}/og-image.png`,
      },
    ],
  };

  return (
    <script
      type="application/ld+json"
      dangerouslySetInnerHTML={{ __html: jsonLd(data) }}
    />
  );
}

/**
 * Per-strategy structured data, plus the breadcrumb that tells Google where the
 * page sits. A breadcrumb is what turns the URL line in a result into
 * "Home > Calculator > Iron Condor" instead of a raw path.
 */
export function StrategyStructuredData({
  slug,
  name,
  description,
}: {
  slug: string;
  name: string;
  description: string;
}) {
  const url = `${branding.canonicalUrl}/calculator/${slug}`;
  const data = {
    '@context': 'https://schema.org',
    '@graph': [
      {
        '@type': 'WebPage',
        '@id': `${url}#page`,
        url,
        name,
        description,
        isPartOf: { '@id': `${branding.canonicalUrl}/#website` },
      },
      {
        '@type': 'BreadcrumbList',
        itemListElement: [
          { '@type': 'ListItem', position: 1, name: 'Home', item: branding.canonicalUrl },
          {
            '@type': 'ListItem',
            position: 2,
            name: 'Strategy calculators',
            item: `${branding.canonicalUrl}/calculator/long-call`,
          },
          { '@type': 'ListItem', position: 3, name, item: url },
        ],
      },
    ],
  };

  return (
    <script
      type="application/ld+json"
      dangerouslySetInnerHTML={{ __html: jsonLd(data) }}
    />
  );
}
