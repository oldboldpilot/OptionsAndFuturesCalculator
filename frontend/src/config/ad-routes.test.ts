/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The rule these tests hold, after the 2026-08-16 AdSense flag:
 *
 *   ADS AND PUBLISHER CONTENT LIVE ON THE SAME SCREENS.
 *
 * The tool screens — the home calculator, every `/calculator/<slug>`, and the
 * `/widget` embed — carry no publisher content, so they carry no Google ads.
 * The `/guides` pages carry the writing, so they carry the advertising. That is
 * the same shape mortgagefvcalculator.com uses: measured on 2026-08-16 its own
 * calculator page is 730 words of pure interface and serves no `adsbygoogle`
 * at all.
 *
 * The interesting cases are the near-misses. A rule written as a bare
 * `startsWith` would suppress ads on anything merely beginning with a protected
 * name, and `/guides` must NOT be caught by the `/` entry — that would silently
 * switch off every ad on the site while every page still rendered correctly.
 */
import { describe, it, expect } from 'vitest';
import { NO_AD_ROUTES, noAdsOnRoute } from '@/config/ad-routes';
import { STRATEGY_SLUGS } from '@/config/strategies';

describe('noAdsOnRoute — screens with no publisher content', () => {
  it.each(['/', '/calculator', '/widget', '/privacy', '/terms'])(
    'suppresses ads on the tool or policy screen %s',
    (route) => {
      expect(noAdsOnRoute(route)).toBe(true);
    },
  );

  it('suppresses ads on every exported calculator page', () => {
    // Not a sample: all 26 must be covered, because each one is a screen that
    // renders the workspace and nothing else.
    const serving = STRATEGY_SLUGS.filter((slug) => !noAdsOnRoute(`/calculator/${slug}`));
    expect(serving).toEqual([]);
  });

  it.each(['/widget/', '/widget/embed', '/privacy/', '/calculator/'])(
    'suppresses ads on the sub-path %s',
    (route) => {
      // The export serves both `/privacy` and `/privacy/`, and Auto Ads reads
      // whichever `location.pathname` the browser happens to be on.
      expect(noAdsOnRoute(route)).toBe(true);
    },
  );
});

describe('noAdsOnRoute — screens that carry the writing', () => {
  it.each(['/guides', '/guides/iron-condor', '/guides/long-call'])(
    'allows ads on the content route %s',
    (route) => {
      expect(noAdsOnRoute(route)).toBe(false);
    },
  );

  it('allows ads on every exported guide page', () => {
    const suppressed = STRATEGY_SLUGS.filter((slug) => noAdsOnRoute(`/guides/${slug}`));
    expect(suppressed).toEqual([]);
  });

  /*
   * The one that matters most, and the reason `'/'` is written as an exact
   * match rather than a prefix.
   *
   * `noAdsOnRoute` tests `pathname === route || pathname.startsWith(route + '/')`.
   * For `'/'` the prefix arm becomes `startsWith('//')`, which no real path
   * satisfies — so the entry catches the home page alone. Had it been a plain
   * `startsWith('/')` it would match EVERY path on the site, switching off all
   * advertising everywhere while every page still rendered perfectly. Nothing
   * would look broken and the revenue would simply be zero.
   */
  it('does not let the home-page entry blanket the whole site', () => {
    expect(noAdsOnRoute('/')).toBe(true);
    expect(noAdsOnRoute('/guides')).toBe(false);
    expect(noAdsOnRoute('/guides/collar')).toBe(false);
  });
});

describe('noAdsOnRoute — edges', () => {
  it('does not suppress a route that merely shares a prefix', () => {
    // `/terms` vs `/terminal`, `/calculator` vs `/calculators`. A bare
    // startsWith would catch both and quietly kill ads on real pages.
    expect(noAdsOnRoute('/terminal')).toBe(false);
    expect(noAdsOnRoute('/privacy-policy')).toBe(false);
    expect(noAdsOnRoute('/widgets')).toBe(false);
    expect(noAdsOnRoute('/calculators')).toBe(false);
  });

  it('treats an absent pathname as ad-eligible rather than throwing', () => {
    // `usePathname` can return null before the router resolves. Returning false
    // is the safe direction: the manual unit renders, which is recoverable,
    // whereas throwing inside a render takes the calculator down.
    expect(noAdsOnRoute(null)).toBe(false);
    expect(noAdsOnRoute(undefined)).toBe(false);
    expect(noAdsOnRoute('')).toBe(false);
  });

  /*
   * The regression that created this file.
   *
   * `NO_AD_ROUTES` briefly lived in `AdSlot.tsx`, which carries `'use client'`.
   * The root layout is a SERVER component and imports this list to build the
   * inline Auto Ads guard; importing a value out of a client module hands the
   * server a client-reference proxy instead, so `JSON.stringify` produced the
   * literal `undefined` and the shipped guard read `if(undefined.some(...))`.
   * That throws while the head is parsing, `pauseAdRequests` is never set, and
   * Auto Ads serves on every route this list protects.
   *
   * A unit test importing the module directly always sees the real array, so it
   * cannot reproduce that. What it CAN do is pin the shape the layout depends
   * on; `scripts/check-export.mjs` asserts the emitted guard in `out/`.
   */
  it('serialises to a JSON array literal the inline guard can embed', () => {
    const serialised = JSON.stringify(NO_AD_ROUTES);
    expect(serialised).not.toBe(undefined);
    expect(serialised).toMatch(/^\[".+"\]$/);
    expect(JSON.parse(serialised)).toEqual(['/', '/calculator', '/widget', '/privacy', '/terms']);
  });
});
