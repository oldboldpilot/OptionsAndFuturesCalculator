/**
 * Routes that must never carry a Google-served ad.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * A PLAIN module, deliberately, and this is the load-bearing part. This list
 * lived in `AdSlot.tsx` for about ten minutes and produced a guard that read
 * `if(undefined.some(...))` in the shipped HTML. `AdSlot.tsx` carries
 * `'use client'`, and when a SERVER component imports a value from a client
 * module it receives a client-reference proxy rather than the value — so
 * `JSON.stringify` of the array returned `undefined`, and the inline Auto Ads
 * guard in the root layout threw a TypeError while the head was still parsing.
 *
 * The failure direction is the bad one: a guard that throws never sets
 * `pauseAdRequests`, so ads would have served on every route listed here,
 * including the /widget embed that was correctly protected before. Keep this
 * file free of `'use client'` and of anything that would require it.
 *
 * Two different reasons for the entries, kept distinct because someone will
 * eventually be tempted to re-enable one:
 *
 *   `/widget` is embedded in <iframe>s on other people's sites. AdSense policy
 *   forbids serving ads inside a frame on pages Google has not authorized, and
 *   an ad inside a third-party embed would also attribute that site's traffic
 *   to this publisher id.
 *
 *   `/privacy` and `/terms` are policy documents, not publisher content. The
 *   "Google-served ads on screens without publisher-content" policy — the one
 *   this site was flagged under on 2026-08-16 — treats legal and navigational
 *   screens as ineligible. They are also the pages a reviewer opens first when
 *   checking whether a site discloses its advertising and cookie use.
 */
export const NO_AD_ROUTES = ['/widget', '/privacy', '/terms'] as const;

/**
 * Shared by the two enforcement points that must agree: `AdSlot` (manual units)
 * and the inline `pauseAdRequests` guard in the root layout (Auto Ads, which
 * places units wherever it likes and would otherwise ignore anything AdSlot
 * decides). Suppressing one and not the other still leaves ads on the page —
 * Auto Ads is the half that is easy to forget.
 */
export function noAdsOnRoute(pathname: string | null | undefined): boolean {
  if (!pathname) return false;
  return NO_AD_ROUTES.some(
    (route) => pathname === route || pathname.startsWith(`${route}/`),
  );
}
