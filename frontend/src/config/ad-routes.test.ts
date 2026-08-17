/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The rule these tests hold:
 *
 *   ADS AND PUBLISHER CONTENT LIVE ON THE SAME SCREENS.
 *
 * The tool screens — the home calculator, every `/calculator/<slug>`, and the
 * `/widget` embed — carry no publisher content, so they carry no Google ads.
 * The 26 `/guides/<slug>` articles carry the writing, so they carry the
 * advertising. That is the shape mortgagefvcalculator.com uses: measured on
 * 2026-08-16 its own calculator page is 730 words of pure interface and serves
 * no `adsbygoogle` at all.
 *
 * THIS FILE WAS REWRITTEN ON 2026-08-17, AND THE REASON IS THE POINT.
 *
 * It used to test a DENYLIST: `/`, `/calculator`, `/widget`, `/privacy`,
 * `/terms`. Every assertion passed. The list was correct for every route anyone
 * had thought of — and a 404's `location.pathname` is whatever the visitor
 * typed, so it matched nothing and was ad-eligible. `/this-page-does-not-exist`
 * served page-level Auto Ads and a multiplex unit on thirteen words of error
 * text, live, a day after the publisher-content fix was believed complete.
 *
 * A denylist can only protect what somebody enumerated. The tests could not
 * catch it because they were written from the same list as the code — asking
 * whether the enumeration was implemented, never whether it was complete.
 *
 * So the module is an ALLOWLIST now, and these tests are written to ask the
 * complementary question: what does an UNANTICIPATED route get? The 404 cases
 * below are the ones that matter.
 */
import { describe, it, expect } from 'vitest';
import { AD_ROUTE_PREFIX, adsOnRoute, noAdsOnRoute } from '@/config/ad-routes';
import { STRATEGY_SLUGS } from '@/config/strategies';

describe('adsOnRoute — the screens that carry the writing', () => {
  it('allows ads on every exported guide article', () => {
    const suppressed = STRATEGY_SLUGS.filter((slug) => !adsOnRoute(`/guides/${slug}`));
    expect(suppressed).toEqual([]);
  });

  it.each(['/guides/iron-condor', '/guides/long-call', '/guides/collar'])(
    'allows ads on the article %s',
    (route) => {
      expect(adsOnRoute(route)).toBe(true);
    },
  );
});

describe('adsOnRoute — the screens that do not', () => {
  it.each(['/', '/calculator', '/widget', '/privacy', '/terms'])(
    'suppresses ads on the tool or policy screen %s',
    (route) => {
      expect(adsOnRoute(route)).toBe(false);
    },
  );

  it('suppresses ads on every exported calculator page', () => {
    // Not a sample: all 26, because each renders the workspace and nothing else.
    const serving = STRATEGY_SLUGS.filter((slug) => adsOnRoute(`/calculator/${slug}`));
    expect(serving).toEqual([]);
  });

  /*
   * The guides INDEX, deliberately excluded.
   *
   * It is a directory of 26 links. The policy names three conditions and the
   * third is "screens used for alerts, navigation or other behavioral
   * purposes" — a link directory is navigation, and the ~300 words of
   * introduction above it do not change what the page is for. One page out of
   * 27, on an account that has already been flagged.
   *
   * Enforced structurally rather than by this check: the loader ships from
   * `app/guides/[strategy]/layout.tsx`, one segment BELOW `app/guides`, so the
   * index is outside the subtree that carries ad code.
   */
  it.each(['/guides', '/guides/'])('suppresses ads on the guides index %s', (route) => {
    expect(adsOnRoute(route)).toBe(false);
  });
});

describe('adsOnRoute — the 404, which is why this is an allowlist', () => {
  /*
   * The defect, stated as a test.
   *
   * Cloudflare Workers static assets serves `404.html` for any unmatched path,
   * and the document's `location.pathname` is the path that was REQUESTED. Under
   * the old denylist every one of these returned "ads permitted", and the
   * measured result was the AdSense loader, an `enable_page_level_ads` push and
   * a multiplex `<ins>` on a page whose own content is one sentence.
   *
   * Mutation check: replacing `adsOnRoute` with the old
   * `!NO_AD_ROUTES.some(r => p === r || p.startsWith(r + '/'))` turns every one
   * of these green-to-red, which is the production symptom exactly.
   */
  it.each([
    '/this-page-does-not-exist',
    '/blog',
    '/en/long-call',
    '/index.php',
    '/wp-admin',
    '/guides-old/iron-condor',
  ])('refuses ads on the unmatched path %s, which resolves to the 404 screen', (route) => {
    expect(adsOnRoute(route)).toBe(false);
  });

  /*
   * A 404 UNDER the ad-serving prefix is the one case that still resolves to
   * "ads permitted", and it is accepted rather than fixed here.
   *
   * `/guides/not-a-strategy` returns the 404 screen with an ad-eligible
   * pathname. It does not matter: `adsOnRoute` is defence in depth, not the
   * mechanism. The 404 is rendered by the ROOT layout, which ships no ad code
   * at all, so there is no loader on that page for an `<ins>` to use — and
   * `AdSlot` is not rendered there either, because the multiplex unit lives in
   * the guides ARTICLE layout, which a 404 never enters.
   *
   * This test is here so that the gap is recorded as understood rather than
   * discovered later and mistaken for the same bug coming back.
   */
  it('would permit a unit under /guides/<unknown>, which the layout split makes moot', () => {
    expect(adsOnRoute('/guides/not-a-strategy')).toBe(true);
  });
});

describe('adsOnRoute — edges', () => {
  it('does not let a shared prefix leak ads onto a different route', () => {
    expect(adsOnRoute('/guidesomething')).toBe(false);
    expect(adsOnRoute('/guides-index')).toBe(false);
    expect(adsOnRoute('/calculator/guides/long-call')).toBe(false);
  });

  it('treats an absent pathname as ad-INELIGIBLE', () => {
    /*
     * The direction flipped with the allowlist, and it is now the safe one.
     *
     * `usePathname` can return null before the router resolves. Under the
     * denylist that meant "not a protected route", so the unit rendered; under
     * the allowlist it means "not a known article", so it does not. Failing
     * closed costs one impression on a route that was about to resolve anyway.
     */
    expect(adsOnRoute(null)).toBe(false);
    expect(adsOnRoute(undefined)).toBe(false);
    expect(adsOnRoute('')).toBe(false);
  });

  it('exposes the prefix the layout split is built on', () => {
    // If this changes, `app/guides/[strategy]/layout.tsx` has to move with it —
    // the runtime check and the file that actually ships the loader are two
    // statements of one rule and must not drift.
    expect(AD_ROUTE_PREFIX).toBe('/guides/');
  });

  it('keeps noAdsOnRoute an exact negation', () => {
    for (const route of ['/', '/guides', '/guides/collar', '/nonsense', null]) {
      expect(noAdsOnRoute(route)).toBe(!adsOnRoute(route));
    }
  });
});
