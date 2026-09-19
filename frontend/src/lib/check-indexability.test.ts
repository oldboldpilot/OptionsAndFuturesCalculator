/**
 * Unit tests for indexability assertions and pure parsing helpers.
 *
 * @author Olumuyiwa Oluwasanmi
 */
import { describe, it, expect } from 'vitest';
import {
  extractCanonicals,
  extractRobotsDirectives,
  hasNoindex,
  parseRobotsTxt,
  matchesDisallowRule,
  isPathDisallowed,
  parseSitemap,
  normalizePath,
  resolveSitemapFile,
  EXCLUDED_PAGES,
  EXPECTED_CANONICAL_DOMAIN,
} from '../../scripts/check-indexability.mjs';

describe('extractCanonicals — pure HTML parsing', () => {
  it('extracts a single canonical URL', () => {
    const html = `
      <!DOCTYPE html>
      <html>
        <head>
          <title>Test Page</title>
          <link rel="canonical" href="https://optionsandfuturescalculator.com/calculator/long-call"/>
        </head>
        <body>Content</body>
      </html>
    `;
    expect(extractCanonicals(html)).toEqual([
      'https://optionsandfuturescalculator.com/calculator/long-call',
    ]);
  });

  it('detects a page with two canonicals (defect case)', () => {
    const html = `
      <!DOCTYPE html>
      <html>
        <head>
          <link rel="canonical" href="https://optionsandfuturescalculator.com/calculator/long-call"/>
          <link rel="canonical" href="https://optionsandfuturescalculator.com/"/>
        </head>
        <body>Content</body>
      </html>
    `;
    const canonicals = extractCanonicals(html);
    expect(canonicals).toHaveLength(2);
    expect(canonicals).toEqual([
      'https://optionsandfuturescalculator.com/calculator/long-call',
      'https://optionsandfuturescalculator.com/',
    ]);
  });

  it('ignores canonical links embedded inside <script> blocks', () => {
    const html = `
      <!DOCTYPE html>
      <html>
        <head>
          <link rel="canonical" href="https://optionsandfuturescalculator.com/guides"/>
        </head>
        <body>
          <script>
            self.__next_f.push([1, '{"rel":"canonical","href":"https://optionsandfuturescalculator.com/wrong"}']);
          </script>
        </body>
      </html>
    `;
    expect(extractCanonicals(html)).toEqual(['https://optionsandfuturescalculator.com/guides']);
  });

  it('ignores non-canonical link tags such as stylesheet and icon', () => {
    const html = `
      <link rel="icon" href="/favicon.ico"/>
      <link rel="stylesheet" href="/style.css"/>
      <link rel="canonical" href="https://optionsandfuturescalculator.com/terms"/>
    `;
    expect(extractCanonicals(html)).toEqual(['https://optionsandfuturescalculator.com/terms']);
  });

  it('returns an empty array when no canonical is present', () => {
    const html = `<html><head><title>404</title></head><body>Not Found</body></html>`;
    expect(extractCanonicals(html)).toEqual([]);
  });
});

describe('hasNoindex and extractRobotsDirectives', () => {
  it('identifies a noindex page with "noindex, follow"', () => {
    const html = `
      <html>
        <head>
          <meta name="robots" content="noindex, follow"/>
        </head>
      </html>
    `;
    expect(hasNoindex(html)).toBe(true);
    expect(extractRobotsDirectives(html)).toContain('noindex');
    expect(extractRobotsDirectives(html)).toContain('follow');
  });

  it('identifies a noindex page with standalone "noindex"', () => {
    const html = `<meta name="robots" content="noindex"/>`;
    expect(hasNoindex(html)).toBe(true);
  });

  it('identifies a noindex page declared via googlebot meta tag', () => {
    const html = `<meta name="googlebot" content="noindex"/>`;
    expect(hasNoindex(html)).toBe(true);
  });

  it('identifies "none" directive as noindex', () => {
    const html = `<meta name="robots" content="none"/>`;
    expect(hasNoindex(html)).toBe(true);
  });

  it('returns false for indexable pages with "index, follow"', () => {
    const html = `<meta name="robots" content="index, follow"/>`;
    expect(hasNoindex(html)).toBe(false);
  });

  it('returns false when no robots meta is present', () => {
    const html = `<html><head><title>Standard</title></head></html>`;
    expect(hasNoindex(html)).toBe(false);
  });

  it('ignores meta tags inside script blocks', () => {
    const html = `
      <html>
        <head><meta name="robots" content="index, follow"/></head>
        <body><script>const m = '<meta name="robots" content="noindex"/>';</script></body>
      </html>
    `;
    expect(hasNoindex(html)).toBe(false);
  });
});

describe('parseRobotsTxt and Disallow matching', () => {
  const robotsTxtSample = `
    User-Agent: *
    Allow: /
    Disallow: /api/
    Disallow: /admin/
    Disallow: /cdn-cgi/

    Sitemap: https://optionsandfuturescalculator.com/sitemap.xml
  `;

  it('parses disallow rules, allow rules, and sitemap URLs', () => {
    const parsed = parseRobotsTxt(robotsTxtSample);
    expect(parsed.disallow).toEqual(['/api/', '/admin/', '/cdn-cgi/']);
    expect(parsed.allow).toEqual(['/']);
    expect(parsed.sitemaps).toEqual(['https://optionsandfuturescalculator.com/sitemap.xml']);
  });

  it('matches a path by a Disallow prefix (/cdn-cgi/)', () => {
    expect(matchesDisallowRule('/cdn-cgi/l/email-protection', '/cdn-cgi/')).toBe(true);
    expect(isPathDisallowed('/cdn-cgi/l/email-protection', ['/api/', '/cdn-cgi/'])).toBe('/cdn-cgi/');
  });

  it('matches a path by a Disallow prefix (/calculator/)', () => {
    expect(matchesDisallowRule('/calculator/long-call', '/calculator/')).toBe(true);
    expect(isPathDisallowed('/calculator/long-call', ['/calculator/'])).toBe('/calculator/');
  });

  it('matches a path by a Disallow prefix (/api/)', () => {
    expect(matchesDisallowRule('/api/quotes', '/api/')).toBe(true);
  });

  it('does NOT match paths outside the disallow prefix', () => {
    const rules = ['/api/', '/admin/', '/cdn-cgi/'];
    expect(isPathDisallowed('/', rules)).toBeNull();
    expect(isPathDisallowed('/guides', rules)).toBeNull();
    expect(isPathDisallowed('/guides/long-call', rules)).toBeNull();
    expect(isPathDisallowed('/calculator/long-call', rules)).toBeNull();
    expect(isPathDisallowed('/privacy', rules)).toBeNull();
    expect(isPathDisallowed('/terms', rules)).toBeNull();
  });

  it('handles wildcard and end anchor rules correctly', () => {
    expect(matchesDisallowRule('/private/secret.pdf', '/private/*.pdf')).toBe(true);
    expect(matchesDisallowRule('/private/secret.html', '/private/*.pdf')).toBe(false);
    expect(matchesDisallowRule('/exact', '/exact$')).toBe(true);
    expect(matchesDisallowRule('/exact/sub', '/exact$')).toBe(false);
  });
});

describe('normalizePath — trailing slashes and index.html equivalences', () => {
  it('treats root variations as "/"', () => {
    expect(normalizePath('/')).toBe('/');
    expect(normalizePath('')).toBe('/');
    expect(normalizePath('/index.html')).toBe('/');
  });

  it('treats trailing slash as identical to bare path', () => {
    expect(normalizePath('/calculator/long-call/')).toBe('/calculator/long-call');
    expect(normalizePath('/calculator/long-call')).toBe('/calculator/long-call');
  });

  it('treats nested /index.html as identical to directory path', () => {
    expect(normalizePath('/calculator/long-call/index.html')).toBe('/calculator/long-call');
  });

  it('preserves distinct subpaths', () => {
    expect(normalizePath('/guides')).toBe('/guides');
    expect(normalizePath('/guides/')).toBe('/guides');
    expect(normalizePath('/guides/iron-condor')).toBe('/guides/iron-condor');
  });
});

describe('Self-canonical validation logic', () => {
  it('passes when canonical pathname exactly matches the page own URL', () => {
    const pageUrl = new URL('https://optionsandfuturescalculator.com/calculator/long-call');
    const canonicalUrl = new URL('https://optionsandfuturescalculator.com/calculator/long-call');
    expect(normalizePath(canonicalUrl.pathname)).toBe(normalizePath(pageUrl.pathname));
    expect(canonicalUrl.hostname).toBe(EXPECTED_CANONICAL_DOMAIN);
    expect(canonicalUrl.protocol).toBe('https:');
  });

  it('passes when one has a trailing slash and the other does not', () => {
    const pageUrl = new URL('https://optionsandfuturescalculator.com/calculator/long-call');
    const canonicalUrl = new URL('https://optionsandfuturescalculator.com/calculator/long-call/');
    expect(normalizePath(canonicalUrl.pathname)).toBe(normalizePath(pageUrl.pathname));
  });

  it('detects a canonical pointing elsewhere (defect case: calculator pointing to homepage)', () => {
    const pageUrl = new URL('https://optionsandfuturescalculator.com/calculator/long-call');
    const canonicalUrl = new URL('https://optionsandfuturescalculator.com');
    expect(normalizePath(canonicalUrl.pathname)).not.toBe(normalizePath(pageUrl.pathname));
  });

  it('detects a canonical pointing to a different domain', () => {
    const canonicalUrl = new URL('https://otherdomain.com/calculator/long-call');
    expect(canonicalUrl.hostname).not.toBe(EXPECTED_CANONICAL_DOMAIN);
  });

  it('detects a canonical that is not https', () => {
    const canonicalUrl = new URL('http://optionsandfuturescalculator.com/calculator/long-call');
    expect(canonicalUrl.protocol).not.toBe('https:');
  });
});

describe('parseSitemap — <loc> extraction', () => {
  it('extracts all <loc> URLs from XML', () => {
    const xml = `
      <?xml version="1.0" encoding="UTF-8"?>
      <urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
        <url><loc>https://optionsandfuturescalculator.com</loc></url>
        <url><loc>https://optionsandfuturescalculator.com/guides</loc></url>
        <url><loc>https://optionsandfuturescalculator.com/calculator/long-call</loc></url>
      </urlset>
    `;
    expect(parseSitemap(xml)).toEqual([
      'https://optionsandfuturescalculator.com',
      'https://optionsandfuturescalculator.com/guides',
      'https://optionsandfuturescalculator.com/calculator/long-call',
    ]);
  });
});

describe('resolveSitemapFile — mapping URLs to exported files', () => {
  it('maps homepage to index.html', () => {
    expect(resolveSitemapFile('https://optionsandfuturescalculator.com')).toBe('index.html');
    expect(resolveSitemapFile('https://optionsandfuturescalculator.com/')).toBe('index.html');
  });

  it('maps /guides to guides.html', () => {
    expect(resolveSitemapFile('https://optionsandfuturescalculator.com/guides')).toBe('guides.html');
  });

  it('maps /calculator/<slug> to calculator/<slug>.html', () => {
    expect(resolveSitemapFile('https://optionsandfuturescalculator.com/calculator/long-call')).toBe(
      'calculator/long-call.html',
    );
  });
});

describe('EXCLUDED_PAGES contract', () => {
  it('contains widget.html, 404.html, and _not-found.html with written reasons', () => {
    expect(EXCLUDED_PAGES.has('widget.html')).toBe(true);
    expect(EXCLUDED_PAGES.get('widget.html')?.reason).toBe('embeddable, noindex');
    expect(EXCLUDED_PAGES.get('widget.html')?.expectNoindex).toBe(true);

    expect(EXCLUDED_PAGES.has('404.html')).toBe(true);
    expect(EXCLUDED_PAGES.get('404.html')?.reason).toBe('error page');
    expect(EXCLUDED_PAGES.get('404.html')?.expectNoindex).toBe(true);

    expect(EXCLUDED_PAGES.has('_not-found.html')).toBe(true);
    expect(EXCLUDED_PAGES.get('_not-found.html')?.reason).toBe('error page');
    expect(EXCLUDED_PAGES.get('_not-found.html')?.expectNoindex).toBe(true);
  });
});
