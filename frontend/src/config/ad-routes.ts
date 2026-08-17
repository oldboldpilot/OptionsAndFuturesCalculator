/**
 * Where Google-served ads are allowed. Exactly one prefix.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * An ALLOWLIST, and the inversion from the first version is the whole point of
 * this file. It was a denylist — `/`, `/calculator`, `/widget`, `/privacy`,
 * `/terms` — which is only correct if you can enumerate every screen the site
 * will ever serve. You cannot, and the screen that was missed is the one nobody
 * writes down: the 404.
 *
 * A 404's `location.pathname` is whatever the visitor typed, so it matches no
 * entry in any denylist. `/this-page-does-not-exist` was therefore ad-eligible:
 * a thirteen-word error screen carrying a multiplex unit and page-level Auto
 * Ads. That is the flagged policy text almost verbatim — a screen without
 * content, used for alerts. Measured live on 2026-08-17, still serving, a day
 * after the first fix was deployed and believed complete.
 *
 * An allowlist fails the other way. An unanticipated route gets no ads until
 * somebody decides it should: that costs impressions, and it cannot cost a
 * policy strike.
 *
 * `/guides` itself is deliberately NOT included. It is a directory of
 * twenty-six links — a navigation screen, which is the third condition the
 * policy names — and its ~300 words of introduction do not change what the page
 * is FOR. One page of twenty-seven, and cheap insurance on an account that has
 * already been flagged once.
 *
 * A PLAIN module, with no `'use client'`, and that still matters. This list
 * lived in `AdSlot.tsx` for one build and produced `if(undefined.some(...))` in
 * the shipped HTML: a server component importing a value from a client module
 * receives a client-reference proxy, not the value. Keep this file importable
 * from a server component.
 */
export const AD_ROUTE_PREFIX = '/guides/';

/**
 * True only for an individual strategy guide.
 *
 * This is defence in depth, NOT the mechanism. The AdSense loader is emitted by
 * `app/guides/[strategy]/layout.tsx` and nowhere else, so a page outside that
 * subtree carries no Google ad code at all — there is nothing to suppress at
 * runtime because nothing was shipped. Compare the old design, where every page
 * on the site loaded the loader and a runtime pathname test decided whether it
 * was allowed to fill: that test is what the 404 walked straight through.
 *
 * What this function still does: stops `AdSlot` rendering an `<ins>` that would
 * have no loader to fill it, and gives the tests something to state the rule
 * against.
 */
export function adsOnRoute(pathname: string | null | undefined): boolean {
  if (!pathname) return false;
  // The length test excludes `/guides/` itself — the index under a trailing
  // slash, which the export serves alongside `/guides`.
  return pathname.startsWith(AD_ROUTE_PREFIX) && pathname.length > AD_ROUTE_PREFIX.length;
}

/**
 * Kept as the negation so call sites that read naturally as "suppress here" do
 * not have to spell `!adsOnRoute(...)`. `AdSlot` and `SiteNav` both ask the
 * question in that direction.
 */
export function noAdsOnRoute(pathname: string | null | undefined): boolean {
  return !adsOnRoute(pathname);
}
