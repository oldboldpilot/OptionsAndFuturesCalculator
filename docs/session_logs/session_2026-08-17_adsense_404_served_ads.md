# The 404 was serving ads: a denylist cannot protect the route nobody writes down

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-17
**Site:** optionsandfuturescalculator.com
**Trigger:** the AdSense violation notice was still live, a day after
[the publisher-content fix](session_2026-08-16_adsense_publisher_content.md) and
[the guides split](session_2026-08-16_theme_layout_and_guides_tab.md) were
deployed and believed complete.

---

## 1. The finding

The first assumption to test was that the notice was merely stale — no review
had been requested, so the flag would stand regardless. That is true, and it is
also not the whole answer. Sweeping the live site found a screen that was still
serving the violation:

```
GET https://optionsandfuturescalculator.com/this-page-does-not-exist

404
  adsbygoogle loader          present
  enable_page_level_ads       present
  multiplex <ins> (7620187871) rendered on mount
  own content                 13 words — "404: This page could not be found"
```

Same for `/guides/nope`, `/wp-admin`, `/blog`, `/en/long-call` — every unmatched
path on the site. Cloudflare Workers static assets serves `404.html` for
anything it cannot route, and that page inherited the root layout, which carried
Google's ad code.

This is the notice's own text on two of its three conditions at once: a screen
**without content**, and one **used for alerts**.

## 2. Why the previous fix did not cover it, and why the tests did not catch it

The root layout loaded `adsbygoogle.js` on every page and decided at runtime
whether it was allowed to fill:

```js
if (["/","/calculator","/widget","/privacy","/terms"].some(r =>
      location.pathname === r || location.pathname.indexOf(r+'/') === 0))
  { (window.adsbygoogle=window.adsbygoogle||[]).pauseAdRequests = 1 }
```

**A 404's `location.pathname` is whatever the visitor typed.** It matches no
entry, so the guard did not fire and the page was ad-eligible.

The guard was not broken. It was correct, it was present in the emitted bytes,
`check-export.mjs` verified it named all five routes, and `ad-routes.test.ts`
asserted every one of them plus the near-miss cases (`/terminal` vs `/terms`,
`/calculators` vs `/calculator`). All of it passed. All of it was true.

**A denylist can only protect routes somebody enumerated, and the tests were
written from the same list as the code.** They could ask whether the enumeration
was implemented; they could not ask whether it was complete. That is the general
shape of this defect, and it is worth more than the specific fix: *a test derived
from the same enumeration as the code under test verifies transcription, not
coverage.*

The error page is the route nobody writes down — and it is the one a crawler
following dead links, stale backlinks and renamed slugs hits most often.

## 3. The fix: emit ad code only where ads belong

Not another guard. The AdSense loader and the `enable_page_level_ads` push moved
out of the shared root layout into `src/app/guides/[strategy]/layout.tsx`, which
is Next's documented mechanism for loading a third-party script on a subset of
routes (`node_modules/next/dist/docs/01-app/02-guides/scripts.md`, "Layout
Scripts"). A raw `<script>` rather than `next/script`, because this is a static
export and the tag has to be in the HTML the CDN serves.

A page outside that subtree now carries **no Google ad code at all**. There is
nothing to suppress and no runtime behaviour that has to be correct.

Four details, each of which could be got wrong:

- **The segment is `guides/[strategy]`, not `guides`.** One level up would also
  cover the `/guides` index — a directory of 26 links, which is navigation, the
  policy's third condition. Its ~300 words of introduction do not change what
  the page is for. One page of 27 given up, on an account already flagged once.
- **`ad-routes.ts` became an ALLOWLIST** — `AD_ROUTE_PREFIX = '/guides/'`,
  `adsOnRoute()`. It fails the opposite way: an unanticipated route gets no ads
  until somebody decides it should. That costs impressions; it cannot cost a
  policy strike. `AdSlot` consumes it as defence in depth, not as the mechanism.
- **The multiplex unit moved with the loader.** It was rendered by the ROOT
  layout, which is how it reached the 404. Keeping the unit beside the code that
  fills it means the two cannot ship to different sets of pages, which is the
  drift that produced this.
- **Site verification is unaffected**, which is what made removing the loader
  from the shared layout safe. `<meta name="google-adsense-account">` stays in
  the root layout on all 59 exported pages. It asserts ownership; the loader
  requests ads. Two different tags, and only one of them had to move.

### `not-found.tsx`, which did not exist

The export was shipping Next's default 404 — one sentence, no links. Beyond the
ads question the screen was simply useless: someone who mistypes a URL or follows
a link to a renamed slug got a dead end.

It now renders real orientation: both tabs, and five popular strategies each
linked to its guide and its calculator, with `robots: { index: false, follow:
true }`. 350 words. It carries no ads regardless of how good it gets — an error
page is navigational by definition, and content does not change that.

## 4. The gates, rewritten to ask the other question

**`check-export.mjs`** used to verify the guard was present and named all five
routes — which it was, and did, *on the 404 too*. It now:

- sweeps **every `.html` under `out/`** rather than a hand-written page list.
  The list was complete for every route anybody had thought of, and silent about
  `404.html` and `_not-found.html`;
- asserts ad code (loader, page-level push, `data-ad-slot`) appears on the 26
  articles and **nowhere else**;
- asserts it has not *vanished* from the articles — a nested-layout refactor can
  silently stop emitting it, and a site with no ad code anywhere fails as
  revenue rather than as an error;
- fails by name if `pauseAdRequests` reappears, since its return means the
  denylist came back.

Mutation-checked in both directions against the built output:

```
loader restored on 404.html      → ✗ ad-code-off-content: 404.html contains
                                     AdSense loader but carries no article
loader removed from an article   → ✗ no-ad-code-on-content: guides/collar.html
                                     is an ad-serving page but carries no loader
```

**`ad-routes.test.ts`** was rewritten to ask about unanticipated routes first.
Six 404 paths are asserted by name; replacing `adsOnRoute` with the old
denylist predicate turns all six red, reproducing the production symptom. One
test records an accepted gap rather than a fix: `/guides/not-a-strategy` is
still "ads permitted" by the function, and it does not matter, because the 404 it
resolves to is rendered by the root layout and has no loader on it. Recorded so
it is not later mistaken for the bug returning.

## 5. Verification

Live, after `wrangler deploy`, cache-busted:

| route | status | loader | page-level push | words |
| --- | --- | --- | --- | --- |
| `/this-page-does-not-exist` | 404 | 0 | 0 | 350 |
| `/guides/nope` | 404 | 0 | 0 | 350 |
| `/wp-admin` | 404 | 0 | 0 | 350 |
| `/` | 200 | 0 | 0 | 757 |
| `/calculator/iron-condor` | 200 | 0 | 0 | 763 |
| `/guides` | 200 | 0 | 0 | 1033 |
| `/guides/iron-condor` | 200 | **1** | **1** | 1013 |
| `/widget` | 200 | 0 | 0 | 236 |
| `/privacy` | 200 | 0 | 0 | 701 |

```
check-export: OK — 26 article pages carry the AdSense loader, all clear 600
words and are distinct; 33 other pages (including 404) carry NO ad code of any
kind; 26 strategies cross-linked both ways.
```

166 frontend tests pass; `tsc --noEmit` clean.

## 6. The review, requested 2026-08-18

The owner requested the AdSense review on **2026-08-18**, against the site as
deployed above. It is their account and their call, which is why it was not
done for them; the difference from the previous day's request is that a real
defect was found and fixed between the two, rather than the same site being
re-submitted unchanged.

**The verdict, whenever it lands, is judging that deploy** — commit `58b4866`,
the state the table in section 5 measures. If anything ships between now and
the outcome, re-measure before concluding the review passed or failed on what
is currently live: a reviewer looking at a stale crawl and a fresh `curl` can
legitimately disagree, and only the second is evidence about today's bytes.
