/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Route matching for the ad suppression list. The interesting cases here are
 * the near-misses: a rule written as a bare `startsWith` would suppress ads on
 * any route that merely BEGINS with a protected name, and `/terms` sits one
 * character away from a hypothetical `/terminal`. Getting that wrong costs
 * revenue silently rather than failing loudly.
 */
import { describe, it, expect } from 'vitest';
import { NO_AD_ROUTES, noAdsOnRoute } from '@/config/ad-routes';

describe('noAdsOnRoute', () => {
  it.each(['/widget', '/privacy', '/terms'])('suppresses ads on %s', (route) => {
    expect(noAdsOnRoute(route)).toBe(true);
  });

  it.each(['/widget/', '/widget/embed', '/privacy/'])(
    'suppresses ads on the sub-path %s',
    (route) => {
      // The static export serves both `/privacy` and `/privacy/`, and Auto Ads
      // reads whichever `location.pathname` the browser is on.
      expect(noAdsOnRoute(route)).toBe(true);
    },
  );

  it.each(['/', '/calculator/long-call', '/calculator/iron-condor'])(
    'allows ads on the content route %s',
    (route) => {
      expect(noAdsOnRoute(route)).toBe(false);
    },
  );

  it('does not suppress a route that merely shares a prefix', () => {
    // `/terms` vs `/terminal`, `/privacy` vs `/privacy-policy-generator`. A
    // bare startsWith would catch both and quietly kill ads on real pages.
    expect(noAdsOnRoute('/terminal')).toBe(false);
    expect(noAdsOnRoute('/privacy-policy')).toBe(false);
    expect(noAdsOnRoute('/widgets')).toBe(false);
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
   * The regression that prompted this file.
   *
   * `NO_AD_ROUTES` briefly lived in `AdSlot.tsx`, which carries `'use client'`.
   * The root layout is a SERVER component and imports this list to build the
   * inline Auto Ads guard; importing a value out of a client module hands the
   * server a client-reference proxy instead, so `JSON.stringify` produced the
   * literal `undefined` and the shipped guard read `if(undefined.some(...))`.
   * That throws while the head is parsing, `pauseAdRequests` is never set, and
   * Auto Ads serves on every route this list is supposed to protect — strictly
   * worse than before, because /widget had been correctly excluded.
   *
   * A unit test importing the module directly always sees the real array, so it
   * cannot reproduce that. What it CAN do is pin the shape the layout depends
   * on, and `scripts/check-export.mjs` asserts the emitted guard in `out/`.
   */
  it('serialises to a JSON array literal the inline guard can embed', () => {
    const serialised = JSON.stringify(NO_AD_ROUTES);
    expect(serialised).not.toBe(undefined);
    expect(serialised).toMatch(/^\[".+"\]$/);
    expect(JSON.parse(serialised)).toEqual(['/widget', '/privacy', '/terms']);
  });
});
