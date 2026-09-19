#!/usr/bin/env node
/**
 * Post-build assertions on search indexing, canonicals, sitemap, and robots.txt.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The invariants this script enforces on the emitted `out/` directory:
 *
 * 1. SITEMAP MATCHES THE EXPORT, both directions.
 *    Every `<loc>` in `out/sitemap.xml` resolves to an exported `.html` file;
 *    every exported `.html` is in the sitemap EXCEPT an explicit EXCLUDED map
 *    where each entry carries a written reason (widget = embeddable, noindex;
 *    404 and _not-found = error pages).
 *
 * 2. NO SITEMAP URL IS NOINDEX.
 *    Parse the robots meta of each page the sitemap lists; any noindex is a
 *    failure. Conversely, every EXCLUDED noindex page must actually carry
 *    noindex.
 *
 * 3. NO SITEMAP URL IS ROBOTS-DISALLOWED.
 *    Parse `out/robots.txt` Disallow rules and match them against every sitemap
 *    path. A blocked page can never show search crawlers its noindex or content.
 *
 * 4. SELF-CANONICAL.
 *    Every page in the sitemap must carry exactly one rel=canonical, absolute,
 *    https, on optionsandfuturescalculator.com, whose pathname equals its own
 *    URL (treating `/index.html` and a trailing slash as the same path).
 *    Any page canonicalising elsewhere (e.g. root layout default regression to
 *    the homepage) fails. The 404 page must NOT carry a canonical.
 *
 * 5. ROBOTS.TXT ITSELF.
 *    Contains a Sitemap: line with the absolute https URL of the sitemap that
 *    actually exists in `out/`, and Disallows `/cdn-cgi/`.
 *
 * 6. POSITIVE CONTROLS.
 *    Guarantees none of the above pass vacuously: >50 exported pages swept,
 *    >50 sitemap entries parsed, and at least one Disallow rule read.
 */
import { readFileSync, existsSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const EXPECTED_CANONICAL_DOMAIN = 'optionsandfuturescalculator.com';

/**
 * Pages deliberately excluded from `sitemap.xml`.
 * Each entry MUST carry a written rationale and an `expectNoindex` flag.
 */
export const EXCLUDED_PAGES = new Map([
  [
    'widget.html',
    {
      reason: 'embeddable, noindex',
      expectNoindex: true,
    },
  ],
  [
    '404.html',
    {
      reason: 'error page',
      expectNoindex: true,
    },
  ],
  [
    '_not-found.html',
    {
      reason: 'error page',
      expectNoindex: true,
    },
  ],
]);

/**
 * Extracts all rel=canonical href values from an HTML string,
 * ignoring link tags inside <script> or <style> blocks.
 */
export function extractCanonicals(html) {
  const cleanHtml = html
    .replace(/<script[\s\S]*?<\/script>/gi, ' ')
    .replace(/<style[\s\S]*?<\/style>/gi, ' ');

  const linkMatches = cleanHtml.matchAll(/<link\b([^>]*?)\/?>/gi);
  const canonicals = [];

  for (const match of linkMatches) {
    const attrs = match[1];
    const relMatch = attrs.match(/\brel\s*=\s*["']?([^"'\s>]+)["']?/i);
    if (!relMatch) continue;

    const relValues = relMatch[1].toLowerCase().split(/\s+/);
    if (!relValues.includes('canonical')) continue;

    const hrefMatch = attrs.match(/\bhref\s*=\s*["']([^"']*)["']/i);
    if (hrefMatch) {
      canonicals.push(hrefMatch[1].trim());
    } else {
      canonicals.push('');
    }
  }

  return canonicals;
}

/**
 * Extracts robots meta directive tokens from an HTML string
 * (inspecting meta tags with name="robots" or name="googlebot").
 */
export function extractRobotsDirectives(html) {
  const cleanHtml = html
    .replace(/<script[\s\S]*?<\/script>/gi, ' ')
    .replace(/<style[\s\S]*?<\/style>/gi, ' ');

  const metaMatches = cleanHtml.matchAll(/<meta\b([^>]*?)\/?>/gi);
  const directives = [];

  for (const match of metaMatches) {
    const attrs = match[1];
    const nameMatch = attrs.match(/\bname\s*=\s*["']?([^"'\s>]+)["']?/i);
    if (!nameMatch) continue;

    const name = nameMatch[1].toLowerCase();
    if (name === 'robots' || name === 'googlebot') {
      const contentMatch = attrs.match(/\bcontent\s*=\s*["']([^"']*)["']/i);
      if (contentMatch) {
        const parts = contentMatch[1].toLowerCase().split(/[\s,]+/);
        for (const part of parts) {
          const trimmed = part.trim();
          if (trimmed) directives.push(trimmed);
        }
      }
    }
  }

  return directives;
}

/**
 * Returns true if the HTML explicitly declares noindex (or none).
 */
export function hasNoindex(html) {
  const directives = extractRobotsDirectives(html);
  return directives.includes('noindex') || directives.includes('none');
}

/**
 * Parses robots.txt content into disallow rules, allow rules, and sitemap URLs.
 */
export function parseRobotsTxt(content) {
  const lines = content.split(/\r?\n/);
  const disallow = [];
  const allow = [];
  const sitemaps = [];

  for (const rawLine of lines) {
    const line = rawLine.replace(/#.*$/, '').trim();
    if (!line) continue;
    const colonIdx = line.indexOf(':');
    if (colonIdx === -1) continue;

    const directive = line.slice(0, colonIdx).trim().toLowerCase();
    const value = line.slice(colonIdx + 1).trim();

    if (directive === 'disallow') {
      if (value) disallow.push(value);
    } else if (directive === 'allow') {
      if (value) allow.push(value);
    } else if (directive === 'sitemap') {
      if (value) sitemaps.push(value);
    }
  }

  return { disallow, allow, sitemaps };
}

/**
 * Matches a URL pathname against a robots.txt Disallow rule (prefix or wildcard).
 */
export function matchesDisallowRule(pathname, rule) {
  if (!rule) return false;
  if (!rule.includes('*') && !rule.endsWith('$')) {
    return pathname.startsWith(rule);
  }
  const hasDollar = rule.endsWith('$');
  const cleanRule = hasDollar ? rule.slice(0, -1) : rule;
  const escaped = cleanRule
    .replace(/[.+?^${}()|[\]\\]/g, '\\$&')
    .replaceAll('*', '.*');
  const pattern = hasDollar ? `^${escaped}$` : `^${escaped}`;
  return new RegExp(pattern).test(pathname);
}

/**
 * Returns the first Disallow rule that blocks the pathname, or null if allowed.
 */
export function isPathDisallowed(pathname, disallowRules) {
  for (const rule of disallowRules) {
    if (matchesDisallowRule(pathname, rule)) {
      return rule;
    }
  }
  return null;
}

/**
 * Parses all <loc> elements out of sitemap XML.
 */
export function parseSitemap(xml) {
  const matches = [...xml.matchAll(/<loc>([\s\S]*?)<\/loc>/gi)];
  return matches.map((m) => m[1].trim());
}

/**
 * Normalizes URL pathnames: removes trailing slashes and trailing /index.html.
 * '/' and '/index.html' both normalize to '/'.
 */
export function normalizePath(p) {
  if (!p || p === '/' || p === '/index.html') return '/';
  let clean = p.replace(/\/+$/, '');
  if (clean.endsWith('/index.html')) {
    clean = clean.slice(0, -'/index.html'.length);
  }
  if (!clean.startsWith('/')) {
    clean = '/' + clean;
  }
  return clean || '/';
}

/**
 * Maps a sitemap URL to its corresponding emitted HTML file path inside `out/`.
 */
export function resolveSitemapFile(loc, outDir) {
  let url;
  try {
    url = new URL(loc);
  } catch {
    return null;
  }
  const pathname = url.pathname;
  if (!pathname || pathname === '/' || pathname === '/index.html') {
    return 'index.html';
  }
  const clean = pathname.replace(/^\/+/, '').replace(/\/+$/, '');
  if (clean.endsWith('.html')) {
    return clean;
  }
  const candidate1 = `${clean}.html`;
  if (!outDir || existsSync(join(outDir, candidate1))) {
    return candidate1;
  }
  const candidate2 = `${clean}/index.html`;
  if (existsSync(join(outDir, candidate2))) {
    return candidate2;
  }
  return candidate1;
}

/**
 * Audits the static export directory for indexability invariants.
 */
export function auditIndexability(outDir = 'out') {
  const failures = [];
  const fail = (check, detail) => failures.push(`${check}: ${detail}`);

  // Enumerate all exported HTML files in out/
  const allPages = readdirSync(outDir, { recursive: true })
    .map((f) => String(f).replaceAll('\\', '/'))
    .filter((f) => f.endsWith('.html'));

  const sitemapPath = join(outDir, 'sitemap.xml');
  let sitemapEntries = [];
  if (!existsSync(sitemapPath)) {
    fail('sitemap-missing', `${sitemapPath} does not exist`);
  } else {
    const sitemapXml = readFileSync(sitemapPath, 'utf8');
    sitemapEntries = parseSitemap(sitemapXml);
  }

  const robotsPath = join(outDir, 'robots.txt');
  let robotsInfo = { disallow: [], allow: [], sitemaps: [] };
  if (!existsSync(robotsPath)) {
    fail('robots-missing', `${robotsPath} does not exist`);
  } else {
    const robotsTxt = readFileSync(robotsPath, 'utf8');
    robotsInfo = parseRobotsTxt(robotsTxt);
  }

  /* ---- 1. SITEMAP MATCHES THE EXPORT, both directions ------------------- */

  const sitemapResolvedFiles = new Set();
  for (const loc of sitemapEntries) {
    const resolvedFile = resolveSitemapFile(loc, outDir);
    if (!resolvedFile || !existsSync(join(outDir, resolvedFile))) {
      fail(
        'sitemap-missing-file',
        `${loc} resolves to ${resolvedFile} which does not exist in ${outDir}/`,
      );
    } else {
      sitemapResolvedFiles.add(resolvedFile);
      if (EXCLUDED_PAGES.has(resolvedFile)) {
        fail(
          'sitemap-contains-excluded',
          `${loc} resolves to ${resolvedFile}, which is in EXCLUDED (${EXCLUDED_PAGES.get(resolvedFile).reason})`,
        );
      }
    }
  }

  for (const file of allPages) {
    if (EXCLUDED_PAGES.has(file)) {
      continue;
    }
    if (!sitemapResolvedFiles.has(file)) {
      fail(
        'unsubmitted-page',
        `${file} is exported in ${outDir}/ but is neither in sitemap.xml nor in EXCLUDED`,
      );
    }
  }

  /* ---- 2. NO SITEMAP URL IS NOINDEX ------------------------------------ */

  for (const loc of sitemapEntries) {
    const resolvedFile = resolveSitemapFile(loc, outDir);
    if (!resolvedFile || !existsSync(join(outDir, resolvedFile))) continue;
    const content = readFileSync(join(outDir, resolvedFile), 'utf8');
    if (hasNoindex(content)) {
      fail(
        'sitemap-page-noindex',
        `${resolvedFile} (listed in sitemap as ${loc}) carries robots noindex`,
      );
    }
  }

  for (const [file, meta] of EXCLUDED_PAGES) {
    if (!meta.expectNoindex) continue;
    if (!existsSync(join(outDir, file))) {
      fail('missing-excluded-file', `${file} is in EXCLUDED but not found in ${outDir}/`);
      continue;
    }
    const content = readFileSync(join(outDir, file), 'utf8');
    if (!hasNoindex(content)) {
      fail(
        'excluded-missing-noindex',
        `${file} is in EXCLUDED (${meta.reason}) but does not carry robots noindex`,
      );
    }
  }

  /* ---- 3. NO SITEMAP URL IS ROBOTS-DISALLOWED -------------------------- */

  for (const loc of sitemapEntries) {
    let pathname;
    try {
      pathname = new URL(loc).pathname;
    } catch {
      continue;
    }
    const matchedRule = isPathDisallowed(pathname, robotsInfo.disallow);
    if (matchedRule) {
      const resolvedFile = resolveSitemapFile(loc, outDir) || loc;
      fail(
        'sitemap-page-disallowed',
        `${resolvedFile} (${loc}, path "${pathname}") is disallowed by robots.txt rule "Disallow: ${matchedRule}"`,
      );
    }
  }

  /* ---- 4. SELF-CANONICAL ----------------------------------------------- */

  for (const loc of sitemapEntries) {
    const resolvedFile = resolveSitemapFile(loc, outDir);
    if (!resolvedFile || !existsSync(join(outDir, resolvedFile))) continue;
    const content = readFileSync(join(outDir, resolvedFile), 'utf8');
    const canonicals = extractCanonicals(content);

    if (canonicals.length === 0) {
      fail('canonical-missing', `${resolvedFile} emits no rel=canonical`);
      continue;
    }
    if (canonicals.length > 1) {
      fail(
        'canonical-multiple',
        `${resolvedFile} emits ${canonicals.length} rel=canonical tags: ${canonicals.join(', ')}`,
      );
      continue;
    }

    const canonicalHref = canonicals[0];
    let canonicalUrl;
    try {
      canonicalUrl = new URL(canonicalHref);
    } catch {
      fail(
        'canonical-invalid',
        `${resolvedFile} canonical "${canonicalHref}" is not a valid absolute URL`,
      );
      continue;
    }

    if (canonicalUrl.protocol !== 'https:') {
      fail('canonical-not-https', `${resolvedFile} canonical is not https: "${canonicalHref}"`);
    }

    if (canonicalUrl.hostname !== EXPECTED_CANONICAL_DOMAIN) {
      fail(
        'canonical-wrong-host',
        `${resolvedFile} canonical host is "${canonicalUrl.hostname}", expected "${EXPECTED_CANONICAL_DOMAIN}"`,
      );
    }

    let sitemapUrl;
    try {
      sitemapUrl = new URL(loc);
    } catch {
      continue;
    }

    const normCanonical = normalizePath(canonicalUrl.pathname);
    const normSitemap = normalizePath(sitemapUrl.pathname);

    if (normCanonical !== normSitemap) {
      fail(
        'canonical-mismatch',
        `${resolvedFile} canonicalises to "${canonicalHref}" (path "${canonicalUrl.pathname}") ` +
          `instead of its own URL "${loc}" (path "${sitemapUrl.pathname}")`,
      );
    }
  }

  // The 404 page (and _not-found) must NOT carry a canonical
  for (const errPage of ['404.html', '_not-found.html']) {
    if (existsSync(join(outDir, errPage))) {
      const errContent = readFileSync(join(outDir, errPage), 'utf8');
      const errCanonicals = extractCanonicals(errContent);
      if (errCanonicals.length > 0) {
        fail(
          'error-page-has-canonical',
          `${errPage} carries rel=canonical (${errCanonicals.join(', ')}), but error pages must not carry a canonical`,
        );
      }
    }
  }

  /* ---- 5. ROBOTS.TXT ITSELF -------------------------------------------- */

  if (existsSync(robotsPath)) {
    if (robotsInfo.sitemaps.length === 0) {
      fail('robots-no-sitemap', 'robots.txt contains no Sitemap: line');
    } else {
      const sitemapLine = robotsInfo.sitemaps[0];
      let sitemapUrl;
      try {
        sitemapUrl = new URL(sitemapLine);
      } catch {
        fail(
          'robots-invalid-sitemap-url',
          `robots.txt Sitemap "${sitemapLine}" is not a valid absolute URL`,
        );
      }

      if (sitemapUrl) {
        if (sitemapUrl.protocol !== 'https:') {
          fail('robots-sitemap-not-https', `robots.txt Sitemap "${sitemapLine}" is not https`);
        }
        if (sitemapUrl.hostname !== EXPECTED_CANONICAL_DOMAIN) {
          fail(
            'robots-sitemap-wrong-host',
            `robots.txt Sitemap host is "${sitemapUrl.hostname}", expected "${EXPECTED_CANONICAL_DOMAIN}"`,
          );
        }
        const sitemapRelPath = sitemapUrl.pathname.replace(/^\/+/, '');
        if (!existsSync(join(outDir, sitemapRelPath))) {
          fail(
            'robots-sitemap-file-missing',
            `robots.txt Sitemap points to "${sitemapLine}" but ${sitemapRelPath} does not exist in ${outDir}/`,
          );
        }
      }
    }

    if (!robotsInfo.disallow.includes('/cdn-cgi/')) {
      fail('robots-missing-disallow-cdn-cgi', 'robots.txt does not Disallow: /cdn-cgi/');
    }
  }

  /* ---- 6. POSITIVE CONTROLS -------------------------------------------- */

  if (allPages.length <= 50) {
    fail(
      'positive-control-pages',
      `expected > 50 exported HTML pages in ${outDir}/, found ${allPages.length}`,
    );
  }
  if (sitemapEntries.length <= 50) {
    fail(
      'positive-control-sitemap',
      `expected > 50 sitemap entries in ${outDir}/sitemap.xml, found ${sitemapEntries.length}`,
    );
  }
  if (robotsInfo.disallow.length < 1) {
    fail(
      'positive-control-disallow',
      `expected at least 1 Disallow rule in robots.txt, found ${robotsInfo.disallow.length}`,
    );
  }

  return {
    failures,
    stats: {
      allPagesCount: allPages.length,
      sitemapCount: sitemapEntries.length,
      excludedCount: EXCLUDED_PAGES.size,
      disallowCount: robotsInfo.disallow.length,
    },
  };
}

export function runCli(outDir = 'out') {
  if (!existsSync(outDir)) {
    console.error(`check-indexability: ${outDir}/ does not exist — run \`npm run build\` first.`);
    process.exit(1);
  }

  const { failures, stats } = auditIndexability(outDir);

  if (failures.length > 0) {
    console.error(`check-indexability: ${failures.length} failure(s)\n`);
    for (const failure of failures) console.error(`  ✗ ${failure}`);
    process.exit(1);
  }

  console.log(
    `check-indexability: OK — ${stats.allPagesCount} exported pages swept; ` +
      `${stats.sitemapCount} sitemap URLs verified self-canonical, indexed, and allowed; ` +
      `${stats.excludedCount} excluded pages verified; ` +
      `robots.txt verified with ${stats.disallowCount} Disallow rule(s) (including /cdn-cgi/) and valid Sitemap directive.`,
  );
}

const isDirectRun = Boolean(
  process.argv[1] &&
  resolve(process.argv[1]) === fileURLToPath(import.meta.url),
);

if (isDirectRun) {
  runCli();
}
