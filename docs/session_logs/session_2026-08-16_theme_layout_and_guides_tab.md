# Dark green, panel sizes, and splitting the guides onto their own tab

@author Olumuyiwa Oluwasanmi

**Date:** 2026-08-16
**Site:** optionsandfuturescalculator.com
**Follows:** [the AdSense publisher-content fix](session_2026-08-16_adsense_publisher_content.md),
same day, same session

Three requests, in order: match mortgagefvcalculator's green, give the cramped
panels their space back, and move the prose onto its own tab the way MFC does.
The third one turned out to interact with the AdSense fix from earlier the same
day, which is the part worth reading.

---

## 1. "The green is too light" — and the accent was already correct

The obvious move was to darken `--color-accent`. It would have changed almost
nothing, and measuring first is what caught that.

Counting painted elements on the live page:

```
--color-profit  → 265 elements
--color-accent  →  23 elements
```

The site is green because of the **profit** token, not the accent one. The
accent tokens already matched mortgagefvcalculator exactly. What a viewer
actually saw as "too light" was `--color-profit` (a mint) plus the two chain
tints, `--color-call-tint` and `--color-atm-tint`, which had that mint baked
into their `rgba()` literals rather than referencing a token at all.

Both themes now use `#008154` for accent and profit, with `#00764c` / `#00603e`
as the respective dims.

**The contrast is 3.83:1 on the dark ground, below AA's 4.5:1 for body copy.**
Taken knowingly and confirmed with the owner: it applies to chips and badges,
which is what `--color-profit` paints. Prose uses the ink tokens.

**Left open, flagged rather than fixed:** gains now read darker than losses on
the dark theme, because `--color-loss` stayed bright. Rebalancing is one line
and nobody has asked for it.

### The wash was the real divergence, and lightningcss ate the tokens

Before the colour change, the body's radial-gradient wash interpolated a
hardcoded `rgba(52, 211, 153, …)` — the old mint, as literals. So the page kept
its light-green cast no matter what the tokens said, on both themes.

Rewriting it as `rgba(var(--rgb-accent), var(--wash-strong))` fixed that, and
exposed a second thing:

**lightningcss drops a custom property nothing USES, and the rule is use, not
value.** `--rgb-profit` and `--rgb-loss` were declared in `:root` and consumed by
nothing once the wash moved to `--rgb-accent`. Both disappeared from the built
CSS entirely.

My first explanation for this was wrong and is recorded because the correction
matters: I said the values were identical across the two theme blocks so the
minifier deduplicated them. `--color-profit-dim` disproves it — `#00764c` dark
against `#00603e` light, different values, dropped just the same. The predicate
is whether anything reads the property.

The dead tokens were deleted rather than re-added.

### A measurement artefact worth remembering

Reading `btnBorder` in the same tick as flipping `data-theme` returned the
*pre-switch* value, because `.btn` transitions `border-color`. It looked exactly
like the light theme having missed the update. Separate-call reads and
screenshots confirmed the values were correct all along. A CSS transition makes
a synchronous computed-style read a statement about the past.

---

## 2. The panels: compression, not scrolling

Requested for the option chain and the probability distribution, then for
Position · legs.

Measured at 1440×900 before:

| panel | body height | what that meant |
| --- | --- | --- |
| Option Chain | 204px | ~7 of 126 strikes, against 3564px of content |
| Probability Distribution | 180px | too short to read as a shape |
| Position · legs | 131px total, 91px body | a panel whose whole job is listing legs |
| Option Ticket | 308px against 327px of content | "Add long call" clipped |
| Exercise & Averaging | 330px against 337px | clipped by 7px |

Two independent causes.

**Column 2 was compressing, not scrolling.** It sets `overflowY: auto`, but its
panels were the flex default `0 1 auto`, so the browser shrank them to fit the
viewport and the column never had anything to scroll. Nothing overflowed;
content was simply cut. Every leg added made it worse, because the compression
is proportional. `flexShrink: 0` on all three fixed it at once, with `flex: 1,
minHeight: 260` on Position so it absorbs spare height and never falls below
roughly three legs.

**The chain's thief was an ad that was not an ad.** A 300×250
`AdSlot size="rectangle"` sat below the metrics panel.
`NEXT_PUBLIC_ADSENSE_SLOT_RECTANGLE` is unset, so `AdSlot` fell to its
unconfigured branch and painted a dashed placeholder — no `<ins>`, no slot id,
nothing served and nothing earned — while holding 250px in a column where the
chain's own body had 204. **The largest single block in the column was an empty
box.** Removing it cost no revenue; the real unit is the multiplex.

After:

| panel | before | after |
| --- | --- | --- |
| Option Chain body | 204px | **462px** |
| Probability Distribution | 180px | **274px** |
| Position · legs | 131px | **260px** |
| Ticket / Exercise | clipped | fixed |

The probability curve also stopped being the smallest region in its own column.
`StrategyWorkspace`'s header comment had said for months that the curve "gets
the largest region rather than being demoted to a panel in a split view", while
the fractions below it gave the matrix 40% and the surface 30% and left the
curve 219px against their 314 and 235. The comment described the intent and the
numbers described a split view. The curve now takes the leftover with a
`minHeight: 240` floor.

Deployed as `b0f093b2` (commit `463b745`).

---

## 3. The guides tab, and why the ads had to move with it

Requested as "like the mortgagefvcalculator uses". `SiteNav` gives the site two
tabs — Calculator | Guides — and the prose moved from below the workspace onto
`/guides` and `/guides/<slug>`.

**The interaction with this morning's AdSense fix is the whole point.** Moving
the writing off the calculator screens while leaving the advertising on them
would have recreated the exact violation that was fixed a few hours earlier: 26
tool screens, no publisher content, Google-served ads.

What settled the direction was measuring the reference rather than reasoning
about it:

```
mortgagefvcalculator.com → 0 occurrences of "adsbygoogle" anywhere
its calculator page      → 730 words, pure interface
```

**MFC can afford a text-light calculator screen precisely because nothing is
sold against it.** So `/` and `/calculator/*` joined `NO_AD_ROUTES`, which is now
five entries: `/`, `/calculator`, `/widget`, `/privacy`, `/terms`.

That makes `/calculator/long-call` and `/calculator/iron-condor` **byte-identical
again, at 763 words** — the original violation, and now correct, because they are
the tool and carry no ads. `check-export.mjs` fails the build if that stops being
true.

### Two URLs per strategy is deliberate

"iron condor calculator" wants the tool; "what is an iron condor" wants the
article. Both are in `sitemap.xml`, and each links to the other — `guideHref`
renders "How this strategy works →" in the workspace header, every guide links
back with "Price a … on live market data →", and `check-export.mjs` asserts both
directions. A split that buries one half is worse than no split.

### The word floor moved 1000 → 600, and why that is not a moved goalpost

The 1000 was set when an ad-serving page was calculator **plus** article — about
750 of those words were interface vocabulary, so it really demanded ~250 words of
prose. The guides are now the article alone: **767 (protective-put) to 954
(long-call) words**, every one of them publisher content, 26 of 26 distinct,
against a flagged baseline of 753 words that contained almost none. Same bar,
honestly re-expressed. Both figures and this reasoning live in the script's own
comment, because a threshold that moves with no stated reason is
indistinguishable from one that moved to make a failure go away.

Deployed as `8d4dbbb3` (commit `f5f0b37`).

---

## 4. Verification

Live, through the CDN:

```
/                          200  words=757   nav=True  guard_ok=True
/guides                    200  words=1034  nav=True  guard_ok=True
/guides/iron-condor        200  words=1013  nav=True  guard_ok=True
/calculator/iron-condor    200  words=763   nav=True  guard_ok=True
```

In the browser, which is the check that matters for Auto Ads:

| page | ad units | `pauseAdRequests` |
| --- | --- | --- |
| `/guides/iron-condor` | 2 (in-article `6395570263`, multiplex `7620187871`) | `false` |
| `/calculator/iron-condor` | 0 | `true` |

Layout at a 720px-tall viewport: nav 36px + workspace 684px = 720 exactly, so
`--nav-h` and the `calc()` agree.

Build gate:

```
check-export: OK — 27 ad-serving pages all clear 600 words and are distinct;
30 tool/policy pages carry no ad unit; guard lists / /calculator /widget
/privacy /terms; 26 strategies cross-linked both ways.
```

161 frontend tests pass. Four of them failed correctly when `NO_AD_ROUTES` grew
from three entries to five — they asserted the old structure — and
`ad-routes.test.ts` was rewritten around the new one, including the case that
matters most: `'/'` is an exact match, never a prefix, because a bare
`startsWith('/')` would switch off advertising on every page of the site while
every page still rendered perfectly.

---

## 5. Deploys and commits

| deploy | commit | change |
| --- | --- | --- |
| `3630d7d7` | `f2c327e` | publisher content on every ad-serving screen |
| `2bdcc5a0` | `6aa076e` | bind the body wash to the theme's own green |
| `9d4e81f8` | `ee34d69` | `#008154` on both themes |
| `b0f093b2` | `463b745` | panel sizes |
| `8d4dbbb3` | `f5f0b37` | guides tab, ads follow the writing |

All attributed solely to Olumuyiwa Oluwasanmi.

## 6. Still the owner's to do

Requesting the AdSense review. It is their account and their call; nothing here
submits it.
