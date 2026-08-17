import AdSlot from '@/components/AdSlot';

/**
 * The ONLY place on this site that ships Google ad code.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Everything under `/guides/<slug>` is a written article — construction, the
 * closed-form payoff identities, Greeks, failure modes, a worked example and
 * FAQs. Ads and publisher content live on the same screens, so the loader lives
 * with the writing rather than in the shared root layout.
 *
 * This is Next's documented mechanism for loading a third-party script on a
 * SUBSET of routes: put it in the layout for that segment, and it loads for
 * that folder's page and any nested route and nowhere else
 * (`node_modules/next/dist/docs/01-app/02-guides/scripts.md`, "Layout
 * Scripts"). A raw <script> is used rather than `next/script` because this is a
 * static export: the tag has to be in the HTML the CDN serves, not scheduled by
 * the client runtime after hydration.
 *
 * WHY IT IS HERE AND NOT IN THE ROOT LAYOUT — the whole point, and the thing to
 * preserve if this file is ever refactored:
 *
 * The root layout carried the loader, an `enable_page_level_ads` push, and an
 * inline guard that set `pauseAdRequests` when `location.pathname` matched a
 * denylist of five routes. That shipped Google's ad code to every page and let
 * a RUNTIME test decide whether it could fill.
 *
 * The 404 defeated it. A 404's pathname is whatever the visitor typed, so it
 * matches no denylist entry:
 *
 *     GET /this-page-does-not-exist  →  404, adsense loader, page-level push,
 *                                       multiplex <ins>, 13 words of content
 *
 * Measured live 2026-08-17. AdSense's notice names that exactly — a screen
 * without content, used for alerts. A denylist protects only routes somebody
 * enumerated, and the error page is the route nobody writes down.
 *
 * Placing the code here inverts it. A page outside this subtree has no ad code
 * at all: nothing to suppress, no guard that has to work, no pathname to match.
 *
 * NOTE the segment this sits in. `app/guides/[strategy]/layout.tsx` wraps the
 * twenty-six ARTICLES only. Putting it one level up, at `app/guides/layout.tsx`,
 * would also cover the `/guides` index — a directory of twenty-six links, which
 * is a navigation screen and the third condition the same policy names. That
 * one level is load-bearing.
 */

const CLIENT = 'ca-pub-3669553016263703';

export default function GuideArticleLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <>
      {/*
        `async`, so third-party code can never delay the article's first paint.

        The loader only makes the API available; the push below is what asks for
        page-level placement. Both are needed — evidence: mortgagefvcalculator.com
        serves on this same publisher id with no <ins> anywhere, and that one
        push is what puts ads on its pages.
      */}
      <script
        async
        src={`https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=${CLIENT}`}
        crossOrigin="anonymous"
      />
      <script
        dangerouslySetInnerHTML={{
          __html:
            `(adsbygoogle=window.adsbygoogle||[]).push({google_ad_client:"${CLIENT}",enable_page_level_ads:true});`,
        }}
      />
      {children}
      {/*
        Multiplex (autorelaxed) — a grid of content recommendations, below the
        article rather than inside it.

        It was in the ROOT layout, which is how it reached the 404. Keeping it
        beside the loader that fills it means the unit and its ad code cannot be
        shipped to different sets of pages, which is the drift that produced the
        violation.
      */}
      <div style={{ maxWidth: '78rem', margin: '0 auto', padding: '1.25rem 1.25rem 0' }}>
        <AdSlot size="multiplex" label="Sponsored" />
      </div>
    </>
  );
}
