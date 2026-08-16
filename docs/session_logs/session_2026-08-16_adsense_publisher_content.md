# AdSense policy violation: ads on screens without publisher-content

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-16
**Site:** optionsandfuturescalculator.com
**Trigger:** AdSense policy notice — *"Google-served ads on screens without
publisher-content"*

---

## 1. The diagnosis, measured

The notice names three conditions: screens without content or with low value
content, screens under construction, and screens used for alerts or navigation.
Rather than guess which applied, the live pages were measured.

```
GET /calculator/long-call    → 753 words of visible text
GET /calculator/iron-condor  → 753 words of visible text
```

Identical, to the word. The two pages differ only by the strategy name in the
`<h1>` and the `<title>`. All 26 `/calculator/<slug>` routes render the same
`StrategyWorkspace` component with one heading string substituted.

Three further facts completed the picture:

- `grep '<p>'` across `src/app/` and `StrategyWorkspace.tsx` returned **nothing**.
  There was not one sentence of prose on any calculator screen.
- The ~750 "words" on each page were ticker symbols, button labels, panel
  headings and the footer disclaimer — interface, not publisher content.
- The home page had **no `<h1>` at all** and linked to none of the 26 strategy
  pages. They were reachable only from `sitemap.xml`, because the in-app strategy
  picker changes client state rather than navigating.

Auto Ads was enabled site-wide from the root layout, so Google was also placing
units on `/privacy` and `/terms`, which are policy documents rather than
publisher content.

`/widget` was already correctly excluded — that part had been done right, and it
is the reason the later regression (§4) was worse than the starting state.

**The conclusion worth carrying: a calculator is not content.** A tool whose
screens carry only its own controls is a screen without publisher content
regardless of how good the tool is. No amount of engine work addresses this; the
fix is writing.

## 2. What was built

| File | Purpose |
| --- | --- |
| `frontend/src/content/strategy-guides.ts` | 26 distinct guides — construction, closed-form max profit / max loss / breakeven, Greeks, when to use, failure modes, worked example, FAQs |
| `frontend/src/components/StrategyGuide.tsx` | Article renderer for a strategy page |
| `frontend/src/components/SiteGuide.tsx` | Home-page content plus the grouped index of all 26 pages |
| `frontend/src/config/ad-routes.ts` | The no-ads route list, shared by both suppression points |
| `frontend/scripts/check-export.mjs` | Post-build assertions on the emitted HTML |

Accuracy rules applied throughout: every identity is stated **at expiry**, **per
share**, ×100 for one standard equity contract, and each worked example is
arithmetic on the identity printed directly above it.

`calendar-spread` and `diagonal-spread` deliberately state that they have **no
closed-form maximum profit**, because the back leg survives the front expiry and
must be valued by a model. Asserting a formula there would have been wrong in a
way that reads as authoritative.

`page.tsx` was converted from a client component to a **server** component. This
is a static export, so anything a crawler or a policy reviewer reads must be in
the HTML the CDN serves, not assembled after hydration.

## 3. Result

| | before | after |
| --- | --- | --- |
| Words per strategy page | 753, identical on all 26 | 1251–1512 |
| Distinct content hashes | 1 of 26 | **26 of 26** |
| Home page `<h1>` | absent | present |
| Home → strategy links | 0 (sitemap only) | 26 |
| Ads on `/privacy`, `/terms` | Auto Ads served | suppressed |

Verified on the live domain after deploy, not only in `out/`.

## 4. The regression this nearly shipped

`NO_AD_ROUTES` was first placed in `AdSlot.tsx`, which carries `'use client'`.
The root layout is a **server** component and imports the list to build the
inline Auto Ads guard. The emitted HTML read:

```js
if(undefined.some(function(r){ ... })){ ...pauseAdRequests=1 }
```

A server component importing a value out of a client module receives a
**client-reference proxy**, not the value, so `JSON.stringify` produced the
literal `undefined`.

The failure direction is the bad one. The guard throws a TypeError while
`<head>` is still parsing, `pauseAdRequests` is never set, and Auto Ads serves
on `/widget`, `/privacy` and `/terms` — **strictly worse than the starting
state**, because `/widget` had been correctly protected before the change.

Nothing in the source looked wrong. No unit test could catch it either: a test
importing the module directly always receives the real array. It was found only
by reading the built artifact.

That is why `check-export.mjs` exists and why `npm run build` now runs it. It
asserts the bytes that are actually served:

1. the Auto Ads guard is present, lists all three routes, and contains no
   `undefined`;
2. every ad-serving page clears a **1000-word floor** — set above the 753 that
   was flagged, so clearing it requires prose rather than more interface;
3. no two strategy pages render identical text;
4. the home page links to all 26;
5. every JSON-LD block parses.

Both directions are mutation-checked. Duplicating one strategy page fires
`duplicate-content` by name; re-introducing the `undefined` guard fires
`auto-ads-guard` with the offending script quoted.

## 5. Edge propagation is not a failed deploy

Immediately after `wrangler deploy`, a sweep of all 26 live pages showed 23 at
1251–1512 words and **three still at 753** — `jade-lizard`, `futures-spread`,
`futures-calendar-spread`.

That reads exactly like three files missing from the upload. It was not. A
cache-busting query string returned the **new** content from the same three
URLs, and a re-sweep shortly after showed all 26 updated (1364, 1306, 1330).

`wrangler.toml` already records this behaviour for asset 404s immediately after
a deploy. It applies to stale HTML too. **Re-measure, and compare the edge
against a cache-busted request, before concluding an upload was missed** — the
wrong conclusion here leads to re-deploying and then to hunting a build problem
that does not exist.

## 6. What was deliberately not done

- **The AdSense review was not requested.** That is a submission to Google on
  the owner's account and is the owner's call, not an automated step.
- `SponsoredBrokers` was left on every page including the policy pages. It is
  this site's own affiliate markup, not Google-served, so the policy does not
  reach it.
- The workspace's `height: 100vh` shell was left alone. Its horizontal-overflow
  behaviour is carefully tuned and documented in place; the article below it is
  reached by a `guideHref` anchor instead, which also works regardless of where
  the pointer sits — every column scrolls internally, so a wheel gesture over a
  panel scrolls the panel rather than the document.
- Three pre-existing lint errors (`react-hooks/purity`, `set-state-in-effect`,
  and a generated `calculator_pb.d.ts` type) were left as found. All predate this
  work; `setMounted` is line 82 in `HEAD`.
