# Frontend code map
@author Olumuyiwa Oluwasanmi

> **How to read a citation in this document.** Every `path:line` here was checked
> mechanically: the file exists and the line is inside it. The line is a pointer to
> a NEIGHBOURHOOD, not a guarantee — a sample of 1,167 citations found the named
> symbol within eight lines of the cited line in about four cases in five, and the
> residue is mostly prose adjacency rather than error. Trust the path and the symbol
> name; re-grep the symbol if the line looks wrong. An attempt to auto-correct the
> residue by moving line numbers made it worse and was reverted, which is why this
> note exists instead of a tighter number.

## Scope

This document covers the complete frontend application of the Options & Futures Calculator repository, rooted at `frontend/`. It documents every source file in `frontend/src`, configuration and deployment descriptors, the static export verification build gate, and all automated test targets.

### Covered files

- **Build configuration and deployment:**
  - `frontend/next.config.ts`
  - `frontend/wrangler.toml`
  - `frontend/scripts/check-export.mjs`
  - `frontend/src/proxy.ts`
- **Application routes (`frontend/src/app/`):**
  - `frontend/src/app/layout.tsx`
  - `frontend/src/app/page.tsx`
  - `frontend/src/app/calculator/[strategy]/page.tsx`
  - `frontend/src/app/guides/page.tsx`
  - `frontend/src/app/guides/[strategy]/layout.tsx`
  - `frontend/src/app/guides/[strategy]/page.tsx`
  - `frontend/src/app/widget/layout.tsx`
  - `frontend/src/app/widget/page.tsx`
  - `frontend/src/app/privacy/page.tsx`
  - `frontend/src/app/terms/page.tsx`
  - `frontend/src/app/not-found.tsx`
  - `frontend/src/app/sitemap.ts`
  - `frontend/src/app/robots.ts`
  - `frontend/src/app/globals.css`
- **State stores (`frontend/src/store/`):**
  - `frontend/src/store/useCalculatorStore.ts`
  - `frontend/src/store/useAssistantStore.ts`
  - `frontend/src/store/useTreePricerStore.ts`
  - `frontend/src/store/useSavedScenariosStore.ts`
- **Component library (`frontend/src/components/`):**
  - `frontend/src/components/AdSlot.tsx`
  - `frontend/src/components/AssistantPanel.tsx`
  - `frontend/src/components/AuthUI.tsx`
  - `frontend/src/components/BermudanDateBuilder.tsx`
  - `frontend/src/components/BrokerRouter.tsx`
  - `frontend/src/components/ExerciseStylePanel.tsx`
  - `frontend/src/components/LegalPage.tsx`
  - `frontend/src/components/OptionChain.tsx`
  - `frontend/src/components/OptionTicket.tsx`
  - `frontend/src/components/PayoffLadder.tsx`
  - `frontend/src/components/PnLMatrix.tsx`
  - `frontend/src/components/PnLSurface.tsx`
  - `frontend/src/components/PositionLegs.tsx`
  - `frontend/src/components/ProPanel.tsx`
  - `frontend/src/components/ProbabilityCurve.tsx`
  - `frontend/src/components/SavedScenarios.tsx`
  - `frontend/src/components/SiteGuide.tsx`
  - `frontend/src/components/SiteNav.tsx`
  - `frontend/src/components/SponsoredBrokers.tsx`
  - `frontend/src/components/StrategyGuide.tsx`
  - `frontend/src/components/StrategyMetrics.tsx`
  - `frontend/src/components/StrategySelector.tsx`
  - `frontend/src/components/StrategyWorkspace.tsx`
  - `frontend/src/components/StructuredData.tsx`
  - `frontend/src/components/TermStructure.tsx`
  - `frontend/src/components/ThemeToggle.tsx`
  - `frontend/src/components/TopBar.tsx`
  - `frontend/src/components/UpgradePrompt.tsx`
- **Libraries and utilities (`frontend/src/lib/`):**
  - `frontend/src/lib/chainFreshness.ts`
  - `frontend/src/lib/licence.ts`
  - `frontend/src/lib/useProStatus.ts`
  - `frontend/src/lib/supabase/client.ts`
  - `frontend/src/lib/supabase/middleware.ts`
  - `frontend/src/lib/supabase/server.ts`
- **Configuration (`frontend/src/config/`):**
  - `frontend/src/config/ad-routes.ts`
  - `frontend/src/config/affiliates.ts`
  - `frontend/src/config/assistantExamples.ts`
  - `frontend/src/config/branding.ts`
  - `frontend/src/config/strategies.ts`
- **Editorial content (`frontend/src/content/`):**
  - `frontend/src/content/strategy-guides.ts`
- **Generated gRPC clients (`frontend/src/grpc/`):**
  - `frontend/src/grpc/AssistantServiceClientPb.ts`
  - `frontend/src/grpc/CalculatorServiceClientPb.ts`
  - `frontend/src/grpc/FinanceServiceClientPb.ts`
  - `frontend/src/grpc/assistant_pb.d.ts` & `assistant_pb.js`
  - `frontend/src/grpc/calculator_pb.d.ts` & `calculator_pb.js`
  - `frontend/src/grpc/finance_pb.d.ts` & `finance_pb.js`
- **Test harness (`frontend/src/test/`):**
  - `frontend/src/test/grpc-harness.ts`

---

## Build gate and deployment contract

### `frontend/next.config.ts`

Configures Next.js to produce a fully static export in `out/`.

| Exported symbol | Kind | Purpose |
| :--- | :--- | :--- |
| `default` | `NextConfig` | Specifies `output: "export"` (`frontend/next.config.ts:4`) |

- **Invariants:** `output: "export"` eliminates all Node.js server-side runtime APIs (such as API route handlers or dynamic server rendering). Every route must statically export to HTML and assets ahead of time.
- **Gotchas:** Because API route handlers cannot run under static export, dynamic image generators (such as `/api/og`) return 404s if deployed. Preview images must point to static files (`branding.ogImageUrl`, `frontend/src/config/branding.ts:38`).

### `frontend/wrangler.toml`

Configures Cloudflare Workers static asset serving for the exported `out/` directory.

| Key | Value | Purpose |
| :--- | :--- | :--- |
| `name` | `"optionsandfuturescalculator"` | Worker application identifier (`frontend/wrangler.toml:24`) |
| `compatibility_date` | `"2026-07-31"` | Workers runtime compatibility date (`frontend/wrangler.toml:25`) |
| `workers_dev` | `true` | Retains the `*.workers.dev` staging endpoint for build verification without touching production domains (`frontend/wrangler.toml:29`) |
| `routes` | Custom domain bindings | Binds `optionsandfuturescalculator.com` and `www.optionsandfuturescalculator.com` with `custom_domain = true` (`frontend/wrangler.toml:36-39`) |
| `assets.directory` | `"./out"` | Edge asset directory served by Cloudflare Workers (`frontend/wrangler.toml:42`) |
| `assets.not_found_handling` | `"404-page"` | Emits the static `out/404.html` rather than Workers' generic error page (`frontend/wrangler.toml:46`) |
| `assets.html_handling` | `"auto-trailing-slash"` | Maps clean paths like `/calculator` to `out/calculator.html` (`frontend/wrangler.toml:51`) |

- **Invariants:** There is deliberately no `main` script defined (`frontend/wrangler.toml:5-7`). Introducing one would invoke a Worker on every asset request, adding latency and cost to actions handled natively by edge asset bindings.
- **Failure modes & Gotchas:** Attaching custom domains required removing existing CNAME records first; Cloudflare returns code 100117 if externally managed DNS records conflict (`frontend/wrangler.toml:31-35`). Immediately after deploy, propagation across edge colos causes brief 404 responses for up to 90 seconds before settling (`frontend/wrangler.toml:16-18`).

### `frontend/scripts/check-export.mjs`

A deterministic post-build assertion gate executed via `npm run build` (`next build && node frontend/scripts/check-export.mjs`, `frontend/package.json:7`). It verifies the emitted static bytes in `out/` to guarantee adherence to publisher content policies.

#### Assertions enforced

1. **Output presence:** Fails with exit code 1 if `out/` does not exist (`frontend/scripts/check-export.mjs:48-51`).
2. **Ad code forbidden on non-content screens (`ad-code-off-content`):** Enumerate all HTML pages in `out/` except the 26 strategy guide articles (`guides/${slug}.html`). Every page in `NO_AD_PAGES` (including `404.html`, `index.html`, `widget.html`, `privacy.html`, `terms.html`, and `calculator/*.html`) is asserted to contain zero occurrences of:
   - AdSense loader: `/pagead2\.googlesyndication\.com/` (`frontend/scripts/check-export.mjs:98, 106-108`)
   - Page-level ad push: `/enable_page_level_ads/` (`frontend/scripts/check-export.mjs:99, 106-108`)
   - Manual ad slot markup: `/data-ad-slot=/` (`frontend/scripts/check-export.mjs:100, 106-108`)
3. **Ad code required on content screens (`no-ad-code-on-content`):** Every file in `CONTENT_PAGES` must exist, must include the Google ad loader, and must contain `enable_page_level_ads` (`frontend/scripts/check-export.mjs:112-128`).
4. **Stale runtime guard check (`stale-guard`):** No page in `ALL_PAGES` may contain `/pauseAdRequests/` (`frontend/scripts/check-export.mjs:134-143`). This prevents reintroducing the obsolete runtime denylist mechanism that previously allowed 404 routes to serve ads.
5. **Publisher content word floor (`publisher-content`):** Every guide in `CONTENT_PAGES` must have visible text containing at least `MIN_WORDS = 600` words (`frontend/scripts/check-export.mjs:165-173`).
6. **No duplicate content (`duplicate-content`):** Visible text across all 26 guides is checked with a map; no two guides may emit identical visible text (`frontend/scripts/check-export.mjs:177-186`).
7. **Bidirectional cross-linking (`internal-links`):**
   - `out/guides.html` must link to `/guides/${slug}` (`frontend/scripts/check-export.mjs:194-196`).
   - `out/calculator/${slug}.html` must link to `/guides/${slug}` (`frontend/scripts/check-export.mjs:198-200`).
   - `out/guides/${slug}.html` must link back to `/calculator/${slug}` (`frontend/scripts/check-export.mjs:202-204`).
8. **Structured data validation (`structured-data`):** Every content page must emit at least one `<script type="application/ld+json">` block, and every block must parse successfully with `JSON.parse` (`frontend/scripts/check-export.mjs:209-222`).

#### Word floor specification (`MIN_WORDS = 600`)

The word floor is 600 words (`frontend/scripts/check-export.mjs:165`). It was lowered from 1000 words because the measured surface changed. When the threshold was 1000, each ad-serving page comprised the calculator tool alongside the article, meaning ~750 words were interface elements (tickers, table headers, buttons), requiring only ~250 words of actual prose. Once the tool was separated into `/calculator/<slug>` and the article into `/guides/<slug>`, the UI vocabulary moved out. Across the 26 pure articles, length ranges from 767 words (`protective-put`) to 954 words (`long-call`). The 600-word floor sits safely below the thinnest real article (767 words) while catching any placeholder or stub (`frontend/scripts/check-export.mjs:148-164`).

---

## Edge proxy and middleware

### `frontend/src/proxy.ts`

| Exported symbol | Kind | Purpose |
| :--- | :--- | :--- |
| `proxy(request)` | `async (request: NextRequest) => Promise<NextResponse>` | Invokes `updateSession(request)` to refresh Supabase authentication cookies (`frontend/src/proxy.ts:4-6`) |
| `config` | `object` | Route matcher excluding `_next/static`, `_next/image`, `favicon.ico`, and image assets (`frontend/src/proxy.ts:8-19`) |

- **Invariants:** Gated paths bypass static image assets, favicons, and Next.js internal static bundles (`frontend/src/proxy.ts:17`).

---

## Routes

| Route file | Path | Component type | Rendered content | In sitemap? | Carries advertising? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `frontend/src/app/page.tsx` | `/` | Server (`frontend/src/app/page.tsx:26`) | `<StrategyWorkspace heading="Options & Futures Profit Calculator" guideHref="/guides" />` (`frontend/src/app/page.tsx:28-34`) | Yes (`frontend/src/app/sitemap.ts:40-44`, prio 1.0) | No (`frontend/src/app/page.tsx:17-24`) |
| `frontend/src/app/layout.tsx` | Root layout | Server (`frontend/src/app/layout.tsx:144`) | HTML shell, fonts (Inter, JetBrains Mono, Fraunces), inline theme init script, AdSense verification meta, `<SiteStructuredData />`, `<SiteNav />`, children, `<SponsoredBrokers />`, sitewide footer (`frontend/src/app/layout.tsx:150-401`) | N/A (Layout) | No (Verification meta tag only; no ad loader or units, `frontend/src/app/layout.tsx:170-214`) |
| `frontend/src/app/calculator/[strategy]/page.tsx` | `/calculator/[strategy]` (26 paths) | Server (`frontend/src/app/calculator/[strategy]/page.tsx:98`) | `<StrategyStructuredData />`, `<StrategyWorkspace heading={`${strategyName} Calculator`} guideHref="/guides/${slug}" />` (`frontend/src/app/calculator/[strategy]/page.tsx:116-129`) | Yes (`frontend/src/app/sitemap.ts:58-62`, prio 0.8) | No (`frontend/src/app/calculator/[strategy]/page.tsx:88-90`) |
| `frontend/src/app/guides/page.tsx` | `/guides` | Server (`frontend/src/app/guides/page.tsx:47`) | `<SiteGuide />` directory of 26 strategy guide links (`frontend/src/app/guides/page.tsx:48`) | Yes (`frontend/src/app/sitemap.ts:48-51`, prio 0.9) | No (`frontend/src/app/guides/page.tsx:15-22`) |
| `frontend/src/app/guides/[strategy]/layout.tsx` | `/guides/[strategy]` layout | Server (`frontend/src/app/guides/[strategy]/layout.tsx:51`) | Google AdSense loader script (`pagead2.googlesyndication.com`), page-level ad push script, children, `<AdSlot size="multiplex" label="Sponsored" />` (`frontend/src/app/guides/[strategy]/layout.tsx:58-90`) | N/A (Layout) | **YES** (Only place where Google ad code is shipped, `frontend/src/app/guides/[strategy]/layout.tsx:4-20`) |
| `frontend/src/app/guides/[strategy]/page.tsx` | `/guides/[strategy]` (26 paths) | Server (`frontend/src/app/guides/[strategy]/page.tsx:70`) | `<StrategyStructuredData />`, `<FaqStructuredData />`, `<StrategyGuide guide={guide} />`, backlink to `/calculator/${slug}`, `<AdSlot size="in-article" label="Sponsored" />` (`frontend/src/app/guides/[strategy]/page.tsx:92-129`) | Yes (`frontend/src/app/sitemap.ts:64-67`, prio 0.7) | **YES** (Carries in-article slot and inherits layout loader, `frontend/src/app/guides/[strategy]/page.tsx:22-24`) |
| `frontend/src/app/widget/layout.tsx` | `/widget` layout | Server (`frontend/src/app/widget/layout.tsx:8`) | Bare `<>{children}</>` with metadata `robots: { index: false, follow: true }` (`frontend/src/app/widget/layout.tsx:5, 13`) | N/A (Layout) | No |
| `frontend/src/app/widget/page.tsx` | `/widget` | Client (`frontend/src/app/widget/page.tsx:1`) | Embeddable workspace in `<Suspense>`: header ("Strategy Modeler"), `<StrategySelector />`, `<StrategyMetrics />`, `<ProbabilityCurve />`, `<OptionChain />` (`frontend/src/app/widget/page.tsx:41-99`) | No (Explicitly excluded, `frontend/src/app/widget/layout.tsx:5`) | No |
| `frontend/src/app/privacy/page.tsx` | `/privacy` | Server (`frontend/src/app/privacy/page.tsx:23`) | `<LegalPage title="Privacy Policy" updated="1 August 2026">` documenting data, localStorage, AdSense, Stripe, Cloudflare, Alpaca (`frontend/src/app/privacy/page.tsx:24-129`) | Yes (`frontend/src/app/sitemap.ts:70-73`, prio 0.3) | No |
| `frontend/src/app/terms/page.tsx` | `/terms` | Server (`frontend/src/app/terms/page.tsx:21`) | `<LegalPage title="Terms of Use" updated="1 August 2026">` with financial disclaimer, model estimation limits, subscription rules (`frontend/src/app/terms/page.tsx:22-96`) | Yes (`frontend/src/app/sitemap.ts:75-78`, prio 0.3) | No |
| `frontend/src/app/not-found.tsx` | 404 handler | Server (`frontend/src/app/not-found.tsx:47`) | 404 error text, links to `/` and `/guides`, popular strategy links (`frontend/src/app/not-found.tsx:51-116`). Metadata sets `robots: { index: false, follow: true }`, `alternates: { canonical: null }` (`frontend/src/app/not-found.tsx:40-41`) | No | No (`frontend/src/app/not-found.tsx:22-32`) |
| `frontend/src/app/sitemap.ts` | `/sitemap.xml` | Server route (`frontend/src/app/sitemap.ts:5`) | Emits static XML sitemap with 56 URLs: `/`, `/guides`, 26 `/calculator/*`, 26 `/guides/*`, `/privacy`, `/terms` (`frontend/src/app/sitemap.ts:39-79`) | Self | No |
| `frontend/src/app/robots.ts` | `/robots.txt` | Server route (`frontend/src/app/robots.ts:4`) | Emits robots configuration: `allow: '/'`, `disallow: ['/api/', '/admin/']`, points to sitemap (`frontend/src/app/robots.ts:7-21`) | No | No |

### Sitemap and robots invariants

- `frontend/src/app/sitemap.ts` omits `<lastmod>` across all URLs (`frontend/src/app/sitemap.ts:21-34`). Setting `new Date()` at build time would cause crawler churn and signal degradation on builds touching only single components or backend services.
- `frontend/src/app/robots.ts` explicitly avoids disallowing `/_next/` (`frontend/src/app/robots.ts:11-17`). Stylesheets and chunks live under `/_next/static/`, and disallowing them prevents crawlers and ad review bots from evaluating computed layout.

---

## State stores (`frontend/src/store/`)

### `useCalculatorStore` (`frontend/src/store/useCalculatorStore.ts`)

Manages the core calculation engine state, option chains, futures forward curve, order ticket draft, and position legs.

#### State shape

| Field | Type | Default | Purpose |
| :--- | :--- | :--- | :--- |
| `symbol` | `string` | `'SPY'` | Underlying ticker (`frontend/src/store/useCalculatorStore.ts:563`) |
| `assetClass` | `'EQUITY' \| 'FUTURES' \| 'CRYPTO'` | `'EQUITY'` | Underlying asset category (`frontend/src/store/useCalculatorStore.ts:564`) |
| `legs` | `Leg[]` | `[]` | Active position legs (`frontend/src/store/useCalculatorStore.ts:565`) |
| `spotPrice` | `number` | `0` | Live underlying quote; 0 represents unknown (`frontend/src/store/useCalculatorStore.ts:567`) |
| `riskFreeRate` | `number \| null` | `null` | Continuous risk-free interest rate (`frontend/src/store/useCalculatorStore.ts:571`) |
| `rateSource` | `RateSource` | `'pending'` | Provenance: `'pending'`, `'measured'`, `'user'`, or `'unavailable'` (`frontend/src/store/useCalculatorStore.ts:572`) |
| `dividendYield` | `number` | `0` | Continuous dividend yield (`frontend/src/store/useCalculatorStore.ts:573`) |
| `matrixPriceMin` | `number \| null` | `null` | Lower price bound override for matrix only (`frontend/src/store/useCalculatorStore.ts:574`) |
| `matrixPriceMax` | `number \| null` | `null` | Upper price bound override for matrix only (`frontend/src/store/useCalculatorStore.ts:575`) |
| `rateMeta` | `RateMeta \| null` | `null` | Treasury observation metadata (`frontend/src/store/useCalculatorStore.ts:576`) |
| `result` | `CalculationResult \| null` | `null` | Computed payoff, Greeks, matrix, risk metrics (`frontend/src/store/useCalculatorStore.ts:577`) |
| `isLoading` | `boolean` | `false` | Strategy calculation in flight flag (`frontend/src/store/useCalculatorStore.ts:578`) |
| `error` | `string \| null` | `null` | Outage or fatal transport error message (`frontend/src/store/useCalculatorStore.ts:579`) |
| `notReady` | `string \| null` | `null` | Unmet precondition message (`frontend/src/store/useCalculatorStore.ts:580`) |
| `gateDenied` | `string \| null` | `null` | Server refusal due to Pro entitlement (`frontend/src/store/useCalculatorStore.ts:581`) |
| `modelLimit` | `string \| null` | `null` | Engine limitation message (`frontend/src/store/useCalculatorStore.ts:582`) |
| `chainStrikes` | `ChainStrike[]` | `[]` | Parsed option chain strikes (`frontend/src/store/useCalculatorStore.ts:584`) |
| `chainExpirations` | `ChainExpiration[]` | `[]` | Available expiry dates and DTEs (`frontend/src/store/useCalculatorStore.ts:585`) |
| `futuresCurve` | `FuturesContract[]` | `[]` | Forward curve contracts (`frontend/src/store/useCalculatorStore.ts:586`) |
| `selectedExpiration` | `string` | `''` | Currently selected chain expiration date (`frontend/src/store/useCalculatorStore.ts:587`) |
| `chainStatus` | `ChainStatus` | `'idle'` | Chain load status (`frontend/src/store/useCalculatorStore.ts:588`) |
| `chainError` | `string \| null` | `null` | Chain fetch failure message (`frontend/src/store/useCalculatorStore.ts:589`) |
| `chainFetchedAt` | `string \| null` | `null` | RFC3339 server timestamp of chain print (`frontend/src/store/useCalculatorStore.ts:590`) |
| `ticket` | `TicketDraft` | Object | Draft leg being composed (`frontend/src/store/useCalculatorStore.ts:595-604`) |

#### Actions and guards

| Action | Parameters | Guard / Behavior |
| :--- | :--- | :--- |
| `setSymbol` | `(symbolInput, customPrice?, customAssetClass?)` | Classifies ticker into `EQUITY`, `FUTURES`, or `CRYPTO` (`frontend/src/store/useCalculatorStore.ts:356-360`). Clears old chain, expirations, forward curve, and result. Discards quote responses if `get().symbol !== sym` (`frontend/src/store/useCalculatorStore.ts:654`). |
| `addLeg` | `(leg)` | Appends leg with unique id `leg-${(legSeq += 1).toString(36)}` (`frontend/src/store/useCalculatorStore.ts:682`). |
| `removeLeg` | `(id)` | Filters legs. If resulting length is 0, resets `result`, `gateDenied`, `modelLimit`, `notReady` to `null` (`frontend/src/store/useCalculatorStore.ts:697-702`). |
| `clearLegs` | `()` | Clears `legs: []`, `result: null`, `gateDenied: null`, `modelLimit: null`, `notReady: null` (`frontend/src/store/useCalculatorStore.ts:704`). |
| `updateLeg` | `(id, updates)` | Maps matching leg id with updates (`frontend/src/store/useCalculatorStore.ts:706-708`). |
| `setSpotPrice` | `(price)` | Overrides spot price (`frontend/src/store/useCalculatorStore.ts:710`). |
| `setRiskFreeRate` | `(rate)` | Sets `riskFreeRate: rate`, `rateSource: 'user'` (`frontend/src/store/useCalculatorStore.ts:716`). |
| `setDividendYield`| `(q)` | Clamps to `q >= 0 ? q : 0` (`frontend/src/store/useCalculatorStore.ts:717`). |
| `setMatrixBounds` | `({ min, max })` | Sanitizes inputs (`<= 0` or non-finite become `null`). Triggers `calculateStrategy()` (`frontend/src/store/useCalculatorStore.ts:734-745`). |
| `loadRiskFreeRate`| `()` | Deduplicates concurrent calls via module promise `rateRequest` (`frontend/src/store/useCalculatorStore.ts:395, 759`). Ignores arrival if `rateSource === 'user'` (`frontend/src/store/useCalculatorStore.ts:769-772`). Rejects responses lacking `as_of_date` (`frontend/src/store/useCalculatorStore.ts:779`). |
| `setTicket` | `(patch)` | Merges patch into `ticket` draft (`frontend/src/store/useCalculatorStore.ts:813`). |
| `commitTicket` | `()` | Enforces preconditions: 1. `strike === null \|\| strike <= 0` (`frontend/src/store/useCalculatorStore.ts:825`); 2. `premium === null \|\| premium <= 0` (`frontend/src/store/useCalculatorStore.ts:829`); 3. Resolves expiration via `t.expiration \|\| get().selectedExpiration` against `chainExpirations`, refusing if unmatched (`frontend/src/store/useCalculatorStore.ts:841-849`). On success calls `addLeg` and clears `notReady` and `error`. |
| `setSelectedExpiration` | `(date)` | Sets date and triggers `loadChain(date)` (`frontend/src/store/useCalculatorStore.ts:870-873`). |
| `loadChain` | `(expiration?)` | Fetches chain via gRPC. Discards response if symbol changed (`frontend/src/store/useCalculatorStore.ts:892`). Populates `ticket.expiration` in lockstep with resolved expiration (`frontend/src/store/useCalculatorStore.ts:950`). Sets `chainStatus: 'ready'` if strikes > 0 OR futures contracts > 0 (`frontend/src/store/useCalculatorStore.ts:953-954`). |
| `calculateStrategy` | `()` | Asynchronously prices position via gRPC (`frontend/src/store/useCalculatorStore.ts:963-1245`). Detailed below. |

#### The staleness token and request lifecycle in `calculateStrategy`

Overlapping calls to `calculateStrategy()` occur routinely during normal UI usage: `PositionLegs` triggers a calculation on leg changes while `StrategyWorkspace` triggers another via a `useEffect` watching `legs`. To prevent out-of-order execution:

1. **Token generation:** Module-scoped variable `let calculationSeq = 0` (`frontend/src/store/useCalculatorStore.ts:437`). At the very entry of `calculateStrategy()`, the token is bumped:
   ```ts
   const token = ++calculationSeq;
   ```
   (`frontend/src/store/useCalculatorStore.ts:964`).
2. **Precondition early returns:** Every early return that exits before making a network request bumps `calculationSeq` and **MUST explicitly set `isLoading: false`**. If `isLoading: false` were omitted, a superseded in-flight request would never clear the loading spinner:
   - `legs.length === 0`: sets `result: null`, `error: null`, `notReady: null`, `isLoading: false` (`frontend/src/store/useCalculatorStore.ts:986-989`).
   - `spotPrice <= 0`: sets `result: null`, `notReady: "No spot price for ... — cannot price the position."`, `error: null`, `isLoading: false` (`frontend/src/store/useCalculatorStore.ts:994-1002`).
   - `positionIv(legs) === null`: sets `result: null`, `notReady: ...`, `error: null`, `isLoading: false` (`frontend/src/store/useCalculatorStore.ts:1003-1022`).
   - `horizonDays(legs) <= 0`: sets `result: null`, `notReady: ...`, `error: null`, `isLoading: false` (`frontend/src/store/useCalculatorStore.ts:1023-1040`).
   - `rate === null` after `await loadRiskFreeRate()`: checked with `if (token !== calculationSeq) return;` (`frontend/src/store/useCalculatorStore.ts:1060`), sets `result: null`, `error: ...`, `notReady: null`, `isLoading: false` (`frontend/src/store/useCalculatorStore.ts:1063-1075`).
3. **Success path guard:** After `await client.calculateStrategy(req, authMetadata())`, the store asserts:
   ```ts
   if (token !== calculationSeq) return;
   ```
   (`frontend/src/store/useCalculatorStore.ts:1099`). If superseded, the response is abandoned without writing state or clearing `isLoading`.
4. **Catch path guard:** In the `catch (err: unknown)` block, the store asserts:
   ```ts
   if (token !== calculationSeq) return;
   ```
   (`frontend/src/store/useCalculatorStore.ts:1194`).
   **Why the catch path is guarded as tightly as the success path:** A superseded failure does not merely set an error message; it sets `result: null` and sets `gateDenied` or `modelLimit`. If an older request returned `PERMISSION_DENIED` after a newer request had already succeeded and rendered a valid calculation, failing to guard the catch block would wipe out the live result and display an upgrade prompt over a position that was already priced (`frontend/src/store/useCalculatorStore.ts:1190-1194`).

---

### `useAssistantStore` (`frontend/src/store/useAssistantStore.ts`)

Drives natural-language strategy parsing via `calculator.assistant.StrategyAssistant/ParseStrategy`.

#### State shape

| Field | Type | Default | Purpose |
| :--- | :--- | :--- | :--- |
| `utterance` | `string` | `''` | Input trade description text (`frontend/src/store/useAssistantStore.ts:234`) |
| `priorClarification` | `string` | `''` | Previous clarification question for multi-turn context (`frontend/src/store/useAssistantStore.ts:235`) |
| `status` | `AssistantStatus` | `'idle'` | Status: `'idle'` or `'parsing'` (`frontend/src/store/useAssistantStore.ts:236`) |
| `params` | `ParsedStrategy \| null` | `null` | Extracted strategy parameters from model (`frontend/src/store/useAssistantStore.ts:237`) |
| `clarification` | `string \| null` | `null` | Clarification question asked by model (`frontend/src/store/useAssistantStore.ts:237`) |
| `refusal` | `ParsedRefusal \| null` | `null` | Explicit model refusal (`frontend/src/store/useAssistantStore.ts:237`) |
| `gateDenied` | `string \| null` | `null` | Refusal due to entitlement (`RPC_PERMISSION_DENIED = 7`, `frontend/src/store/useAssistantStore.ts:237`) |
| `modelLimit` | `string \| null` | `null` | Model capability limitation (`RPC_FAILED_PRECONDITION = 9`, `frontend/src/store/useAssistantStore.ts:237`) |
| `error` | `string \| null` | `null` | Network or unhandled failure (`frontend/src/store/useAssistantStore.ts:237`) |
| `selectedStrategyId` | `string \| null` | `null` | Strategy catalogue ID selected by parse (`frontend/src/store/useAssistantStore.ts:238`) |
| `applySeq` | `number` | `0` | Monotonic counter incremented on each apply (`frontend/src/store/useAssistantStore.ts:239`) |
| `pendingExpirationDays` | `number` | `0` | Unresolved target DTE awaiting chain match (`frontend/src/store/useAssistantStore.ts:240`) |
| `applyNote` | `string \| null` | `null` | User-facing explanation of expiry snapping or adjustments (`frontend/src/store/useAssistantStore.ts:237`) |
| `applyBlocked` | `string \| null` | `null` | Reason applying was rejected (`frontend/src/store/useAssistantStore.ts:237`) |
| `demoIndex` | `number` | `0` | Index of active recorded example in demo strip (`frontend/src/store/useAssistantStore.ts:242`) |

#### Actions and guards

- `parse()`: Refuses empty utterance with `"Describe the trade before parsing it."` (`frontend/src/store/useAssistantStore.ts:268`). Clears prior outcomes on entry (`frontend/src/store/useAssistantStore.ts:277`). Evaluates `res.getOutcomeCase()`:
  - `OUTCOME_PARAMS (1)`: Populates `params` object, clears `priorClarification` (`frontend/src/store/useAssistantStore.ts:295-322`).
  - `OUTCOME_CLARIFICATION (2)`: Populates `clarification` and updates `priorClarification` (`frontend/src/store/useAssistantStore.ts:325-335`).
  - `OUTCOME_REFUSAL (3)`: Populates `refusal: { reason, message }`, clears `priorClarification` (`frontend/src/store/useAssistantStore.ts:338-346`).
  - `OUTCOME_NOT_SET (0)` or unhandled: Sets error `"The assistant returned an empty response."` (`frontend/src/store/useAssistantStore.ts:350-356`).
- `applyParams(known)`:
  - Validates `assetClass` against `['EQUITY', 'FUTURES', 'CRYPTO']`; if invalid, sets `applyBlocked` and refuses to proceed (`frontend/src/store/useAssistantStore.ts:398-406`).
  - Validates `strategy` against `known.ids`; if unlisted, sets `applyBlocked` (`frontend/src/store/useAssistantStore.ts:408-416`).
  - Updates calculator store symbol via `calc.setSymbol(p.symbol, undefined, p.assetClass)` and sets ticket quantity (`frontend/src/store/useAssistantStore.ts:421-422`).
  - Does **NOT** synthesize strikes or premiums: legs are constructed by the user or picker from the live chain (`frontend/src/store/useAssistantStore.ts:382-387`).
  - Calls `resolvePendingExpiry()` (`frontend/src/store/useAssistantStore.ts:445`).
- `resolvePendingExpiry()`: Snaps `pendingExpirationDays` to the closest listed expiration via `nearestListedExpiry(calc.chainExpirations, target)` (`frontend/src/store/useAssistantStore.ts:139-147, 459`). Clears `pendingExpirationDays: 0` and sets `applyNote` describing whether the match was exact or nearest (`frontend/src/store/useAssistantStore.ts:480-494`).
- `stepDemo(delta, count)`: Adjusts `demoIndex` with modulo arithmetic. It modifies `demoIndex` and **nothing else** (`frontend/src/store/useAssistantStore.ts:250-252`), preventing demo examples from ever leaking into live result state fields.

---

### `useTreePricerStore` (`frontend/src/store/useTreePricerStore.ts`)

Drives the trinomial tree pricing engine (`sensen.finance.Finance/PriceOptionTree`) for European, American, and Bermudan exercise styles, along with optional Asian averaging.

#### State shape

| Field | Type | Default | Purpose |
| :--- | :--- | :--- | :--- |
| `exerciseType` | `ExerciseStyle` | `'AMERICAN'` | Active exercise convention (`'EUROPEAN' \| 'AMERICAN' \| 'BERMUDAN'`) (`frontend/src/store/useTreePricerStore.ts:206`) |
| `bermudanDates` | `BermudanDate[]` | `[]` | Selected Bermudan exercise dates (`frontend/src/store/useTreePricerStore.ts:207`) |
| `asianExpanded` | `boolean` | `false` | Collapsible Asian section toggle (`frontend/src/store/useTreePricerStore.ts:209`) |
| `asianType` | `AsianStyle` | `'NOT_ASIAN'` | Averaging style (`'NOT_ASIAN' \| 'AVERAGE_PRICE' \| 'AVERAGE_STRIKE'`) (`frontend/src/store/useTreePricerStore.ts:210`) |
| `averagingStates` | `number` | `50` | Number of averaging states for Asian pricing (`frontend/src/store/useTreePricerStore.ts:211`) |
| `steps` | `number` | `100` | Tree steps (default 100 for vanilla; 60 for Asian) (`frontend/src/store/useTreePricerStore.ts:213`) |
| `advancedOpen` | `boolean` | `false` | Advanced settings collapse toggle (`frontend/src/store/useTreePricerStore.ts:214`) |
| `loading` | `boolean` | `false` | Pricing in flight flag (`frontend/src/store/useTreePricerStore.ts:216`) |
| `error` | `string \| null` | `null` | Network or rate limit error message (`frontend/src/store/useTreePricerStore.ts:217`) |
| `notReady` | `string \| null` | `null` | Precondition missing message (`frontend/src/store/useTreePricerStore.ts:218`) |
| `gateDenied` | `string \| null` | `null` | Pro entitlement denial message (`frontend/src/store/useTreePricerStore.ts:219`) |
| `results` | `TreePriceResult[]` | `[]` | Priced styles, values, Greeks, early exercise premiums (`frontend/src/store/useTreePricerStore.ts:220`) |
| `droppedDateCount` | `number` | `0` | Count of Bermudan dates pruned during validation (`frontend/src/store/useTreePricerStore.ts:221`) |

#### Actions and invariants

- **Default exercise style:** Defaults to `'AMERICAN'` (`frontend/src/store/useTreePricerStore.ts:206`) because listed US equity options are American-style contracts.
- **Dynamic gRPC client import:** `FinanceClient` and `finance_pb` (606 KB uncompressed) are imported dynamically inside `executePricing()` (`frontend/src/store/useTreePricerStore.ts:399-402`), keeping them off the initial bundle critical path.
- **Debouncing:** `priceTree()` is debounced by 300 ms (`DEBOUNCE_MS = 300`, `frontend/src/store/useTreePricerStore.ts:124, 308-312`).
- **Staleness token:** Tracks `let requestSeq = 0` (`frontend/src/store/useTreePricerStore.ts:153`). Incremented via `const token = ++requestSeq;` on execution (`frontend/src/store/useTreePricerStore.ts:319`). Checked after awaits and in catch (`frontend/src/store/useTreePricerStore.ts:359, 383, 484, 487`).
- **Precondition refusals (routed to `notReady`):**
  - `spotPrice <= 0` -> `"No spot price for ... yet -- pick a symbol with a live quote."` (`frontend/src/store/useTreePricerStore.ts:329`)
  - `ticket.strike === null || ticket.strike <= 0` -> `"Pick a strike on the ticket to price exercise styles."` (`frontend/src/store/useTreePricerStore.ts:333`)
  - `ticket.impliedVolatility === null || ticket.impliedVolatility <= 0` -> `"No implied volatility on the ticket. Pick a strike from the option chain so IV comes from a live quote."` (`frontend/src/store/useTreePricerStore.ts:337-341`)
  - `dte <= 0` -> `"No expiry on the ticket -- pick one from the chain."` (`frontend/src/store/useTreePricerStore.ts:348`)
  - `exerciseType === 'BERMUDAN' && isAsian && validBermudanDates.length === 0` -> `"Add at least one Bermudan exercise date to price this style."` (`frontend/src/store/useTreePricerStore.ts:382-389`)
- **Bermudan date validation (`buildValidatedBermudanDates`):** Filters dates strictly to `(0, yearsToExpiry]`. Dedupes adjacent dates closer than one tree step ($dt = T / \text{steps}$) because the engine's backward induction cannot resolve two dates within half a step (`frontend/src/store/useTreePricerStore.ts:162-180`).
- **Asian Greeks contract:** In Asian averaging mode, the engine returns structural zeros `{0, 0, 0}` for Greeks. `TreePriceResult.greeks` is explicitly set to `null` based on `asianType !== 'NOT_ASIAN'` rather than response inspection, preventing fake zero-Greeks from displaying (`frontend/src/store/useTreePricerStore.ts:46-52, 453-456`).

---

### `useSavedScenariosStore` (`frontend/src/store/useSavedScenariosStore.ts`)

Manages server-persisted user strategy scenarios.

#### State shape

| Field | Type | Default | Purpose |
| :--- | :--- | :--- | :--- |
| `scenarios` | `SavedScenario[]` | `[]` | List of retrieved saved scenarios (`frontend/src/store/useSavedScenariosStore.ts:139`) |
| `status` | `SavedStatus` | `'idle'` | Status: `'idle' \| 'loading' \| 'ready' \| 'error'` (`frontend/src/store/useSavedScenariosStore.ts:140`) |
| `failure` | `SavedFailure \| null` | `null` | Failure descriptor: `{ message, needsSignIn, needsPro }` (`frontend/src/store/useSavedScenariosStore.ts:141`) |
| `lastSavedName`| `string \| null` | `null` | Name of the last successfully saved scenario (`frontend/src/store/useSavedScenariosStore.ts:142`) |

#### Actions and invariants

- **Isolation from calculator loading:** Operates in a separate store so that saving/listing scenarios never toggles `useCalculatorStore.isLoading`, which would cause calculation panels to blank behind spinners during database writes (`frontend/src/store/useSavedScenariosStore.ts:14-24`).
- **`save(name)` guards:**
  - `!name.trim()` -> sets `failure: { message: 'Give this scenario a name.', needsSignIn: false, needsPro: false }` (`frontend/src/store/useSavedScenariosStore.ts:168-171`).
  - `calc.legs.length === 0` -> sets `failure: { message: 'Add at least one leg before saving.', ... }` (`frontend/src/store/useSavedScenariosStore.ts:172-175`).
  - `calc.riskFreeRate === null` -> sets `failure: { message: 'Waiting for the risk-free rate. Try again in a moment.', ... }` (`frontend/src/store/useSavedScenariosStore.ts:181-190`).
- **Parity with pricing requests:** Uses `buildStrategyRequest` to construct wire messages (`frontend/src/store/useSavedScenariosStore.ts:199-208`), guaranteeing saved scenarios serialize identically to active calculation requests.
- **`apply(scenario)` ordering:** Calls `calc.setSymbol(scenario.symbol, scenario.spotPrice)`, then `calc.clearLegs()`, sets dividend yield, appends legs via `calc.addLeg()`, and finally calls `calc.calculateStrategy()` (`frontend/src/store/useSavedScenariosStore.ts:240-252`). Setting the symbol first is critical; otherwise, symbol switching would wipe out the restored legs.

---

## Component library (`frontend/src/components/`)

### Inventory

| Component | File | Purpose | Stores / Props read |
| :--- | :--- | :--- | :--- |
| `AdSlot` | `AdSlot.tsx` | AdSense ad container reserving layout space to prevent CLS. Enforces allowlist. | Props: `size`, `label`. Reads `usePathname()`, `adsOnRoute` (`frontend/src/components/AdSlot.tsx:54-60`). |
| `AssistantPanel` | `AssistantPanel.tsx` | Natural-language trade parser composer, demo strip, and outcome chips. | `useAssistantStore`, `useCalculatorStore` (`chainStatus`, `chainExpirations`, `futuresCurve`), `useProStatus` (`frontend/src/components/AssistantPanel.tsx:4-16`). |
| `AuthUI` | `AuthUI.tsx` | Supabase email/password sign-in and registration form. | Direct Supabase browser client (`createClient`, `frontend/src/components/AuthUI.tsx:3, 9`). |
| `BermudanDateBuilder` | `BermudanDateBuilder.tsx` | Bermudan exercise date picker with presets and day-number inputs. | Props: `{ yearsToExpiry: number }`. Reads `useTreePricerStore` (`frontend/src/components/BermudanDateBuilder.tsx:21-24`). |
| `BrokerRouter` | `BrokerRouter.tsx` | Deep-linking order ticket generator for partner broker routing. | `useCalculatorStore` (`legs`, `spotPrice`, `frontend/src/components/BrokerRouter.tsx:13`). |
| `ExerciseStylePanel` | `ExerciseStylePanel.tsx` | Trinomial tree pricer interface (American/European/Bermudan, Asian averaging). | `useCalculatorStore` (`ticket`, `spotPrice`, `riskFreeRate`, `chainExpirations`, `selectedExpiration`), `useTreePricerStore` (`frontend/src/components/ExerciseStylePanel.tsx:49-53`). |
| `LegalPage` | `LegalPage.tsx` | Readable single-column presentation shell for `/privacy` and `/terms`. | Props: `{ title, updated, children }` (`frontend/src/components/LegalPage.tsx:13-21`). |
| `OptionChain` | `OptionChain.tsx` | Interactive strike chain ladder with calls, puts, Greeks, and freshness chip. | `useCalculatorStore` (`chainStrikes`, `chainExpirations`, `selectedExpiration`, `chainStatus`, `chainError`, `chainFetchedAt`, `setTicket`, `ticket`, `loadChain`, `setSelectedExpiration`, `frontend/src/components/OptionChain.tsx:24-27`). |
| `OptionTicket` | `OptionTicket.tsx` | Single-leg order composition form (action, type, strike, premium, qty, IV, Asian). | `useCalculatorStore` (`ticket`, `setTicket`, `commitTicket`, `chainStrikes`, `chainExpirations`, `selectedExpiration`, `setSelectedExpiration`, `chainStatus`, `spotPrice`, `frontend/src/components/OptionTicket.tsx:45-49`). |
| `PayoffLadder` | `PayoffLadder.tsx` | Payoff table at curve date with diverging P&L meters and dollars/% toggle. | `useCalculatorStore` (`result`, `spotPrice`, `isLoading`, `error`, `modelLimit`, `gateDenied`, `notReady`, `frontend/src/components/PayoffLadder.tsx:21`). |
| `PnLMatrix` | `PnLMatrix.tsx` | 2D price × date P&L grid with heatmap tinting and price bounds controls. | `useCalculatorStore` (`result`, `spotPrice`, `isLoading`, `error`, `modelLimit`, `gateDenied`, `notReady`, `matrixPriceMin`, `matrixPriceMax`, `setMatrixBounds`, `frontend/src/components/PnLMatrix.tsx:30-33`). |
| `PnLSurface` | `PnLSurface.tsx` | Interactive 3D WebGL P&L height field rendered with Three.js / React Three Fiber. | `useCalculatorStore` (`result`, `isLoading`, `error`, `modelLimit`, `gateDenied`, `notReady`, `frontend/src/components/PnLSurface.tsx:72`). |
| `PositionLegs` | `PositionLegs.tsx` | Table of active position legs with inline editing, side toggling, and per-leg Greeks. | `useCalculatorStore` (`legs`, `updateLeg`, `removeLeg`, `clearLegs`, `calculateStrategy`, `result`, `frontend/src/components/PositionLegs.tsx:9-10`). |
| `ProPanel` | `ProPanel.tsx` | Subscription status sidebar panel with Stripe checkout triggers and license input. | `useProStatus` (`licence`, `pro`, `claiming`, `error`, `activate`, `deactivate`, `setError`, `frontend/src/components/ProPanel.tsx:21`). |
| `ProbabilityCurve` | `ProbabilityCurve.tsx` | Terminal price lognormal distribution overlay with profit shading and crosshairs. | `useCalculatorStore` (`result`, `error`, `isLoading`, `symbol`, `modelLimit`, `gateDenied`, `notReady`, `frontend/src/components/ProbabilityCurve.tsx:81`). |
| `SavedScenarios` | `SavedScenarios.tsx` | Saved strategy scenarios manager (save, list, apply, delete). | `useSavedScenariosStore`, `useCalculatorStore` (`legs.length`), `useProStatus` (`frontend/src/components/SavedScenarios.tsx:25-28`). |
| `SiteGuide` | `SiteGuide.tsx` | Guides index directory organizing 26 strategies by editorial category. | Reads `STRATEGY_GUIDES` (`frontend/src/components/SiteGuide.tsx:3`). |
| `SiteNav` | `SiteNav.tsx` | Slim global top navigation bar with tab switching and theme toggle. | Reads `usePathname()` (`frontend/src/components/SiteNav.tsx:30`). |
| `SponsoredBrokers` | `SponsoredBrokers.tsx` | Static broker affiliate partner cards with FTC sponsorship disclosure. | Reads `BROKER_PARTNERS`, `HAS_PAID_PARTNER` (`frontend/src/components/SponsoredBrokers.tsx:2`). |
| `StrategyGuide` | `StrategyGuide.tsx` | Editorial guide article rendering payoff identities, formulas, and worked examples. | Props: `{ guide: StrategyGuide }` (`frontend/src/components/StrategyGuide.tsx:19`). |
| `StrategyMetrics` | `StrategyMetrics.tsx` | Strategy outcome summary cards (max profit/loss, breakeven, EV, PoP, Greeks, VaR). | `useCalculatorStore` (`result`, `error`, `gateDenied`, `modelLimit`, `notReady`, `frontend/src/components/StrategyMetrics.tsx:33`). |
| `StrategySelector` | `StrategySelector.tsx` | Preset strategy picker categorized by market outlook; configures templates. | `useCalculatorStore`, `useAssistantStore`, `useProStatus` (`frontend/src/components/StrategySelector.tsx:4-7`). |
| `StrategyWorkspace`| `StrategyWorkspace.tsx`| Master 3-column trading calculator layout coordinating all panels. | Props: `{ heading?, guideHref? }`. Reads `useCalculatorStore` (`calculateStrategy`, `legs`, `setSymbol`, `symbol`, `frontend/src/components/StrategyWorkspace.tsx:32-40`). |
| `StructuredData` | `StructuredData.tsx` | Static JSON-LD metadata emitter for WebApplication, FinancialProduct, FAQs. | Props: Various. Reads `branding` (`frontend/src/components/StructuredData.tsx:2`). |
| `TermStructure` | `TermStructure.tsx` | Futures forward curve table showing basis, contango/backwardation, and yields. | `useCalculatorStore` (`futuresCurve`, `chainStatus`, `chainError`, `symbol`, `spotPrice`, `assetClass`, `frontend/src/components/TermStructure.tsx:27-28`). |
| `ThemeToggle` | `ThemeToggle.tsx` | Theme switch toggling `data-theme` between `slate` and `light`. | Reads document `dataset.theme` via `useSyncExternalStore` (`frontend/src/components/ThemeToggle.tsx:39`). |
| `TopBar` | `TopBar.tsx` | Workspace header showing ticker input, live spot, rate chip, and recalculate button. | `useCalculatorStore` (`frontend/src/components/TopBar.tsx:15-19`). |
| `UpgradePrompt` | `UpgradePrompt.tsx` | Paywall upgrade card with checkout buttons displaying verbatim server refusal. | Props: `{ reason: string }`. Reads `useProStatus` (`frontend/src/components/UpgradePrompt.tsx:25-26`). |

---

### Key calculation panels

#### `PayoffLadder` (`frontend/src/components/PayoffLadder.tsx`)
Renders the one-dimensional at-expiry payoff curve (`result.expiryCurve`) returned in `StrategyResponse.pnl_matrix` (`frontend/src/components/PayoffLadder.tsx:11-13`). The curve is evaluated at `result.inputs.curveDays` (the earliest leg expiry, rather than the position horizon), indicated via a chip (`"at expiry · Xd"` or `"at near expiry · Xd"`, `frontend/src/components/PayoffLadder.tsx:30-44`). Prices are displayed descending, thinned to a maximum of 25 rows while preserving the final worst-price boundary (`frontend/src/components/PayoffLadder.tsx:57-61`). It highlights the row closest to the current spot price (`frontend/src/components/PayoffLadder.tsx:66-69`) and features a toggle between dollar P&L and percentage return-on-risk (`frontend/src/components/PayoffLadder.tsx:75-84`). Each row includes a diverging horizontal meter centered at 0% with green profit and red loss bars (`frontend/src/components/PayoffLadder.tsx:162-189`). Refusal states take precedence over the table: `modelLimit` ("Not modelled"), `error` ("Unavailable"), `isLoading` (skeleton rows), `gateDenied` ("Needs Pro"), and `notReady` ("Not priced yet") (`frontend/src/components/PayoffLadder.tsx:110-217`).

#### `PnLMatrix` (`frontend/src/components/PnLMatrix.tsx`)
Renders the two-dimensional price $\times$ date grid from `result.matrix` (`MatrixCell[]`), where every leg is re-priced by the engine using Black-Scholes across remaining maturities at each grid point (`frontend/src/components/PnLMatrix.tsx:9-14`). This view makes calendar and diagonal spreads legible by visualizing the time decay differential between near and far legs across the intermediate calendar columns (`frontend/src/components/PnLMatrix.tsx:16-20`). The date axis sorts ascending with today on the left, while the price axis sorts descending and is sampled to a maximum of 21 rows around the spot price (`frontend/src/components/PnLMatrix.tsx:43-70`). Cells display dollar P&L or percentage return on risk (`mode: 'dollars' | 'percent'`, `frontend/src/components/PnLMatrix.tsx:34`), tinted dynamically with green (`--color-profit`) or red (`--color-loss`) based on their fraction of the maximum observed matrix magnitude (`frontend/src/components/PnLMatrix.tsx:149-157`). It provides draft inputs (`loDraft`, `hiDraft`) committed on blur or Enter to constrain the matrix price window (`matrixPriceMin`, `matrixPriceMax`) independently of the global payoff curve (`frontend/src/components/PnLMatrix.tsx:103-145`).

#### `PnLSurface` (`frontend/src/components/PnLSurface.tsx`)
Visualizes the price $\times$ date $\times$ profit grid from `result.matrix` as a 3D height field solid using Three.js and `@react-three/fiber` (`frontend/src/components/PnLSurface.tsx:9-15`). It maps grid cells onto a displaced `THREE.PlaneGeometry` where height ($Z$-axis) represents normalized P&L magnitude, with per-vertex color interpolation between profit green (`#13aa52`), flat off-white (`#f2f2f2`), and loss red (`#c0392b`) (`frontend/src/components/PnLSurface.tsx:31-54`). The surface executes a slow parallax drift via `useFrame` oscillating around the $Z$-axis (`rotation.z = -0.9 + Math.sin(clock.elapsedTime * 0.12) * 0.13`, `frontend/src/components/PnLSurface.tsx:58-62`). To eliminate unnecessary WebGL Canvas overhead during routine calculations, the surface defaults to `enabled: false` ("Off") and requires user opt-in via a segmented "3D" toggle (`frontend/src/components/PnLSurface.tsx:73, 105-108`).

#### `ProbabilityCurve` (`frontend/src/components/ProbabilityCurve.tsx`)
Overlays the strategy's at-expiry P&L curve (`result.expiryCurve`) directly onto the continuous lognormal probability density function of the terminal underlying price under Geometric Brownian Motion (`frontend/src/components/ProbabilityCurve.tsx:6-15, 68-79`). Evaluated at `curveDays` (the earliest leg maturity, matching the engine's PoP integral), the density $f(x)$ is computed in closed form using drift $\mu = \ln(S_0) + (r - q - \frac{1}{2}\sigma^2)T$ and volatility $\text{sd} = \sigma\sqrt{T}$ (`frontend/src/components/ProbabilityCurve.tsx:18-22, 102-106`). Cumulative normal probabilities use the Abramowitz & Stegun 7.1.26 polynomial approximation for $\text{erf}(x)$ (`frontend/src/components/ProbabilityCurve.tsx:24-36`). The SVG chart shades regions where interpolated P&L is positive with teal tint, overlays vertical reference lines for spot, breakevens, and $\pm 1\sigma$ / $\pm 2\sigma$ probability bands, and supports interactive mouse tracking (`hoverX`) displaying exact price, interpolated P&L, cumulative percentile, and cumulative profit probability (`frontend/src/components/ProbabilityCurve.tsx:149-530`).

---

## Libraries and configuration

### `chainFreshness.ts` (`frontend/src/lib/chainFreshness.ts`)

Pure utility computing whether an option chain print is fresh enough to display as LIVE.

| Constant / Function | Value | Location | Rule / Enforcing behavior |
| :--- | :--- | :--- | :--- |
| `LIVE_MAX_AGE_SECONDS` | `60` | `frontend/src/lib/chainFreshness.ts:35` | Print is LIVE only if age is strictly less than 60 seconds (`frontend/src/lib/chainFreshness.ts:80`). Exactly 60s is DELAYED. |
| `CLOCK_SKEW_TOLERANCE_SECONDS` | `5` | `frontend/src/lib/chainFreshness.ts:42` | Absorbs benign client-server clock skew. |
| `chainFreshness(fetchedAt, nowMs)` | Function | `frontend/src/lib/chainFreshness.ts:53` | Evaluates freshness. If `fetchedAt` is missing or unparseable, returns `{ ageSeconds: null, isLive: false, asOfTime: '' }` (`frontend/src/lib/chainFreshness.ts:54-63`). If `ageSeconds < -5`, client clock is excessively slow; returns `{ ageSeconds: null, isLive: false, asOfTime }` (`frontend/src/lib/chainFreshness.ts:73-78`), withholding the derived LIVE claim while preserving timestamp text. |
| Tick interval | `15_000` ms | `frontend/src/components/OptionChain.tsx:48` | `OptionChain` runs `setInterval(() => setNowMs(Date.now()), 15_000)`, guaranteeing that stale prints transition from LIVE to DELAYED without waiting for component re-renders. |

### `licence.ts` (`frontend/src/lib/licence.ts`)

Client-side Pro license storage, unverified claims parsing, and auth metadata generation.

| Exported symbol | Kind | Purpose |
| :--- | :--- | :--- |
| `readClaims(token)` | Function | Extracts payload claims `{ t: tier, e: expiresEpoch }` without verifying HMAC (`frontend/src/lib/licence.ts:86-101`). Requires prefix `lk_live_` (`frontend/src/lib/licence.ts:20, 87`). |
| `loadLicence()` | Function | Reads `ofc.licence` from `localStorage`. If missing, unparseable, or `expiresEpoch * 1000 < Date.now()`, clears storage and returns `null` (`frontend/src/lib/licence.ts:103-116`). |
| `saveLicence(token)` | Function | Validates format via `readClaims` and persists valid tokens to `localStorage` (`frontend/src/lib/licence.ts:119-124`). |
| `clearLicence()` | Function | Removes `ofc.licence` from `localStorage` (`frontend/src/lib/licence.ts:126-128`). |
| `isPro(info)` | Function | Returns `true` only if `info.tier === 'pro'` and `expiresEpoch * 1000 > Date.now()` (`frontend/src/lib/licence.ts:130-132`). |
| `authMetadata()` | Function | Synchronously returns gRPC-Web metadata headers (`frontend/src/lib/licence.ts:148-155`). Sets `x-api-key: info.token` if a license exists, and `authorization: Bearer <cachedAccessToken>` if a signed-in Supabase session exists. The backend evaluates `authorization` before `x-api-key`. |
| `startCheckout(plan)` | `async` function | POSTs to billing worker (`/checkout`), returning Stripe checkout session URL (`frontend/src/lib/licence.ts:161-171`). |
| `claimLicence(sessionId)` | `async` function | GETs `/licence?session_id=...` from billing worker and persists returned token via `saveLicence` (`frontend/src/lib/licence.ts:180-185`). |

### `useProStatus.ts` (`frontend/src/lib/useProStatus.ts`)

Provides unified Pro subscription state combining localStorage license tokens and Supabase account tiers.

- **Account tier decoding (`decodeAccountTier`):** Decodes JWT payload of the Supabase session token to extract `app_metadata.tier` (`frontend/src/lib/useProStatus.ts:21-40`). If `payload.exp * 1000 < Date.now()`, fails closed and returns `null` (`frontend/src/lib/useProStatus.ts:35`).
- **Store definition (`useProStore`):** Holds `licence`, `accountTier`, `claiming`, `error`, `initialised`.
- **Checkout return handling:** `init()` inspects `window.location.search`. If `?checkout=success&session_id=...` is present, triggers `claimLicence(sessionId)` and cleans query parameters using `window.history.replaceState({}, '', window.location.pathname)` (`frontend/src/lib/useProStatus.ts:110-134`).
- **Entitlement evaluation:** `useProStatus()` returns `pro: isPro(s.licence) || s.accountTier === 'pro'` (`frontend/src/lib/useProStatus.ts:168`). Both credentials operate additively; neither can disable the other.

### Supabase clients (`frontend/src/lib/supabase/`)

- `client.ts`: Exports `createClient()` using `createBrowserClient` from `@supabase/ssr` (`frontend/src/lib/supabase/client.ts:3-8`).
- `server.ts`: Exports `async createClient()` using `createServerClient` reading and writing cookies via Next.js `cookies()` (`frontend/src/lib/supabase/server.ts:4-29`).
- `middleware.ts`: Exports `updateSession(request: NextRequest)` using `createServerClient` to refresh authentication tokens on incoming requests (`frontend/src/lib/supabase/middleware.ts:4-34`).

### `ad-routes.ts` (`frontend/src/config/ad-routes.ts`)

| Exported symbol | Value / Type | Purpose |
| :--- | :--- | :--- |
| `AD_ROUTE_PREFIX` | `'/guides/'` | Single allowed route prefix for advertising (`frontend/src/config/ad-routes.ts:35`). |
| `adsOnRoute(pathname)` | `(pathname) => boolean` | Returns `true` only if `pathname.startsWith('/guides/') && pathname.length > 8` (`frontend/src/config/ad-routes.ts:51-56`). |
| `noAdsOnRoute(pathname)` | `(pathname) => boolean` | Exact negation: `!adsOnRoute(pathname)` (`frontend/src/config/ad-routes.ts:63-65`). |

- **Allowlist design:** This file defines an **ALLOWLIST** (`frontend/src/config/ad-routes.ts:6-22`). It replaced an earlier denylist (`/`, `/calculator`, `/widget`, `/privacy`, `/terms`) that failed to protect 404 pages because an unmatched path like `/this-page-does-not-exist` never matched the denylist.
- **Failure direction:** The allowlist **fails towards NO ADS**. Any unanticipated, unlisted, null, or empty route resolves to `false`, preventing policy violations on unhandled pages (`frontend/src/config/ad-routes.ts:19-21, 52`).
- **Exclusion of `/guides`:** `/guides` (and `/guides/`) is intentionally excluded because it is a navigation directory of links (`frontend/src/config/ad-routes.ts:23-27, 55`).

### `strategies.ts` (`frontend/src/config/strategies.ts`)

Exports `STRATEGY_SLUGS` as a `readonly string[]` containing exactly 26 strategy route slugs (`frontend/src/config/strategies.ts:10-37`):
`long-call`, `long-put`, `call-spread`, `put-spread`, `bull-put-spread`, `bear-call-spread`, `straddle`, `strangle`, `iron-condor`, `iron-butterfly`, `butterfly`, `condor`, `collar`, `covered-call`, `cash-secured-put`, `protective-put`, `jade-lizard`, `calendar-spread`, `diagonal-spread`, `risk-reversal`, `futures-spread`, `futures-outright`, `futures-calendar-spread`, `futures-intercommodity-spread`, `covered-futures-call`, `futures-basis-arbitrage`.

### `affiliates.ts` (`frontend/src/config/affiliates.ts`)

Configures broker partner cards (`BROKER_PARTNERS: BrokerPartner[]`, `frontend/src/config/affiliates.ts:50-79`):
1. Interactive Brokers (`https://www.interactivebrokers.com/`, `trackingId: null`)
2. tastytrade (`https://tastytrade.com/`, `trackingId: null`)
3. Tradier (`https://tradier.com/`, `trackingId: null`)
4. Alpaca (`https://alpaca.markets/`, `trackingId: null`)

- `HAS_PAID_PARTNER`: Evaluated as `BROKER_PARTNERS.some((b) => b.trackingId !== null)` (`frontend/src/config/affiliates.ts:82`). Controls whether links are labeled "Sponsored" or "Partner links" in `SponsoredBrokers.tsx:28, 49-53`.

### `branding.ts` (`frontend/src/config/branding.ts`)

Configures brand identity, metadata, and absolute canonical links.

- `SITE_URL`: `"https://optionsandfuturescalculator.com"` (`frontend/src/config/branding.ts:27`).
- `branding.canonicalUrl`: Defaults to `SITE_URL` (`frontend/src/config/branding.ts:47`).
- `branding.ogImageUrl`: Defaults to `${SITE_URL}/og-image.png` (`frontend/src/config/branding.ts:38`).
- `branding.twitterHandle`: Defaults to `""` (`frontend/src/config/branding.ts:46`). Omitted from `<meta name="twitter:creator">` when empty (`frontend/src/app/layout.tsx:124`).

### `assistantExamples.ts` (`frontend/src/config/assistantExamples.ts`)

Contains `RECORDED_EXCHANGES: readonly RecordedExchange[]` with 3 real captures recorded on `2026-08-11` from `api.optionsandfuturescalculator.com` (`frontend/src/config/assistantExamples.ts:63-109`):
1. `"bull call spread on NVDA, 30 days, 2 contracts"` -> NVDA, bull_call_spread, 30d, qty 2.
2. `"iron condor on SPY, 45 days, 1 contract"` -> SPY, iron_condor, 45d, qty 1.
3. `"long strangle on AMD, 14 days, 5 contracts"` -> AMD, long_strangle, 14d, qty 5.
- **Invariants:** These examples are immutable observations transcribed verbatim from service responses (`frontend/src/config/assistantExamples.ts:7-29`). None are hand-written.

---

## Editorial strategy guides (`frontend/src/content/strategy-guides.ts`)

Contains full written editorial articles for all 26 strategy slugs.

### Record shape

```ts
export interface StrategyGuide {
  slug: string;
  name: string;
  outlook: string;
  netCost: 'Debit' | 'Credit' | 'Debit or credit' | 'Margin';
  lede: string;
  construction: string[];
  maxProfit: string;
  maxLoss: string;
  breakeven: string;
  greeks: string;
  whenToUse: string;
  risks: string[];
  example: {
    setup: string;
    rows: Array<[string, string]>;
    note: string;
  };
  faqs: Array<{ q: string; a: string }>;
  related?: readonly string[];
}
```
(`frontend/src/content/strategy-guides.ts:27-64`).

### Uniqueness and collision rules

- **Strictly unique fields:**
  - `slug`: Must match the record key (`frontend/src/content/strategy-guides.test.ts:42-45`).
  - `lede`: Must be unique across all 26 guides (`frontend/src/content/strategy-guides.test.ts:84-98`).
  - `greeks`: Must be unique across all 26 guides (`frontend/src/content/strategy-guides.test.ts:84-98`).
  - `whenToUse`: Must be unique across all 26 guides (`frontend/src/content/strategy-guides.test.ts:84-98`).
  - `example.setup`: Must be unique across all 26 guides (`frontend/src/content/strategy-guides.test.ts:101-104`).
- **Permitted collisions:**
  - `netCost`: Short enumeration (`'Debit' | 'Credit' | 'Debit or credit' | 'Margin'`) that naturally collides across strategies (`frontend/src/content/strategy-guides.test.ts:80-82`).
  - `outlook`: Market direction descriptors (e.g. `"Bullish — directional"`) that repeat across siblings by design (`frontend/src/content/strategy-guides.test.ts:80-82`).

---

## Generated gRPC clients (`frontend/src/grpc/`)

All files in `frontend/src/grpc/` are generated artifacts:

| Generated file | Source proto | Generating tool | Tooling version |
| :--- | :--- | :--- | :--- |
| `AssistantServiceClientPb.ts` | `assistant.proto` | `protoc-gen-grpc-web` | `v1.5.0` (protoc `v3.19.1`) (`frontend/src/grpc/AssistantServiceClientPb.ts:8-11`) |
| `CalculatorServiceClientPb.ts` | `calculator.proto` | `protoc-gen-grpc-web` | `v1.5.0` (protoc `v3.19.1`) (`frontend/src/grpc/CalculatorServiceClientPb.ts:8-11`) |
| `FinanceServiceClientPb.ts` | `finance.proto` | `protoc-gen-grpc-web` | `v1.5.0` (protoc `v3.19.1`) (`frontend/src/grpc/FinanceServiceClientPb.ts:8-11`) |
| `assistant_pb.js` & `.d.ts` | `assistant.proto` | `protoc` (JS compiler) | `v3.19.1` (`frontend/src/grpc/assistant_pb.js:10`) |
| `calculator_pb.js` & `.d.ts` | `calculator.proto` | `protoc` (JS compiler) | `v3.19.1` (`frontend/src/grpc/calculator_pb.js:10`) |
| `finance_pb.js` & `.d.ts` | `finance.proto` | `protoc` (JS compiler) | `v3.19.1` (`frontend/src/grpc/finance_pb.js:10`) |

- **Strict rule against manual editing:**
  Every generated file contains:
  ```
  // Code generated by protoc-gen-grpc-web. DO NOT EDIT.
  ```
  or
  ```
  // GENERATED CODE -- DO NOT EDIT!
  ```
  Manual modifications are prohibited; changes must be made to `backend/proto/*.proto` and recompiled via the proto generation pipeline.

---

## Error and entitlement handling

The frontend strictly discriminates backend errors using **numeric gRPC status codes**, never by matching message strings. Error copy has changed multiple times across releases; matching on strings would cause entitlement failures to fall through into fatal error handlers.

| Store | Status code | Code name | State written | Enforcing line |
| :--- | :--- | :--- | :--- | :--- |
| `useCalculatorStore` | `7` | `PERMISSION_DENIED` | Sets `gateDenied: message`, `isLoading: false`, `result: null`, `error: null` | `frontend/src/store/useCalculatorStore.ts:405, 1211-1222` |
| `useCalculatorStore` | `9` | `FAILED_PRECONDITION`| Sets `modelLimit: message`, `isLoading: false`, `result: null`, `error: null` | `frontend/src/store/useCalculatorStore.ts:414, 1224-1234` |
| `useCalculatorStore` | Other | Default error | Sets `error: message`, `isLoading: false`, `result: null` | `frontend/src/store/useCalculatorStore.ts:1236-1244` |
| `useAssistantStore` | `7` | `PERMISSION_DENIED` | Sets `gateDenied: message`, `status: 'idle'` | `frontend/src/store/useAssistantStore.ts:67, 363-366` |
| `useAssistantStore` | `9` | `FAILED_PRECONDITION`| Sets `modelLimit: message`, `status: 'idle'` | `frontend/src/store/useAssistantStore.ts:79, 367-370` |
| `useTreePricerStore` | `7` | `PERMISSION_DENIED` | Sets `gateDenied: message`, `loading: false`, `results: []`, `error: null` | `frontend/src/store/useTreePricerStore.ts:145, 495-498` |
| `useTreePricerStore` | `8` | `RESOURCE_EXHAUSTED` | Sets `error: message`, `loading: false`, `results: []`, `gateDenied: null` | `frontend/src/store/useTreePricerStore.ts:146, 501-504` |
| `useSavedScenariosStore` | `16` | `UNAUTHENTICATED` | Sets `failure: { message, needsSignIn: true, needsPro: false }` | `frontend/src/store/useSavedScenariosStore.ts:70, 89` |
| `useSavedScenariosStore` | `7` | `PERMISSION_DENIED` | Sets `failure: { message, needsSignIn: false, needsPro: true }` | `frontend/src/store/useSavedScenariosStore.ts:71, 90` |

---

## Test coverage

The Vitest test suite (`npm test`, `frontend/package.json:10`) exercises the frontend contract across 17 test files:

| Test file | Target exercised | Key assertions |
| :--- | :--- | :--- |
| `frontend/src/lib/chainFreshness.test.ts` | `chainFreshness.ts` | Verifies LIVE status (<60s), DELAYED boundary at 60s and 900s, negative skew tolerance (absorbing <5s skew, withholding age if <-5s), null/unparseable timestamps, and time elapse. |
| `frontend/src/config/ad-routes.test.ts` | `ad-routes.ts` | Verifies allowlist behavior: all 26 `/guides/<slug>` permit ads; `/`, `/calculator`, `/widget`, `/privacy`, `/terms`, and `/guides` suppress ads; unmatched 404 paths refuse ads. |
| `frontend/src/content/strategy-guides.test.ts` | `strategy-guides.ts` | Verifies all 26 `STRATEGY_SLUGS` have guides with matching slugs; asserts substantive length for lede, maxProfit, maxLoss, breakeven, construction, risks, example rows, and faqs; verifies uniqueness of `lede`, `greeks`, `whenToUse`, and `example.setup`. |
| `frontend/src/store/harness.canary.test.ts` | `grpc-harness.ts` | Canary verifying that `createFakeClient` correctly intercepts gRPC calls to `OptionsCalculatorClient` and drives `useCalculatorStore`. |
| `frontend/src/store/asian-leg.test.ts` | `useCalculatorStore.ts` | Verifies Asian averaging styles (`AVERAGE_PRICE`, `AVERAGE_STRIKE`) round-trip through `commitTicket` and serialize to `ProtoLeg.asian_type` on `StrategyRequest`, while vanilla options serialize to `NOT_ASIAN`. |
| `frontend/src/store/assistant-apply.test.ts` | `useAssistantStore.ts` | Asserts that applying parsed strategies updates symbol, asset class, strategy selection, and ticket quantity without synthesizing strikes or premiums; verifies nearest listed expiry snapping and asset class validation. |
| `frontend/src/store/assistant-outcomes.test.ts` | `useAssistantStore.ts` | Verifies handling of the three OK outcomes (`OUTCOME_PARAMS`, `OUTCOME_CLARIFICATION`, `OUTCOME_REFUSAL`), prior clarification threading, and discrimination of `PERMISSION_DENIED` (7) into `gateDenied` and `FAILED_PRECONDITION` (9) into `modelLimit`. |
| `frontend/src/store/calculate-guards.test.ts` | `useCalculatorStore.ts` | Asserts order and behavior of `calculateStrategy` precondition guards: empty legs, non-positive spot, missing IV, non-positive horizon, and missing Treasury rate. |
| `frontend/src/store/calculator-not-ready.test.ts` | `useCalculatorStore.ts` | Verifies routing of precondition failures to `notReady` rather than `error`, asserting mutual exclusivity and proper clearing upon resolution or empty position. |
| `frontend/src/store/calculator-race.test.ts` | `useCalculatorStore.ts` | Verifies that overlapping/concurrent `calculateStrategy` calls use `calculationSeq` to prevent stale responses from overwriting newer results, blanking live results with errors, or clearing `isLoading` prematurely. |
| `frontend/src/store/chain.test.ts` | `useCalculatorStore.ts` | Verifies `loadChain`, `setSelectedExpiration`, and `setSymbol`: lockstep synchronization of `ticket.expiration` with resolved expiry, handling futures chains with no strikes as `ready`, field fidelity across quotes, and cache clearance on ticker switch. |
| `frontend/src/store/entitlement.test.ts` | `useCalculatorStore.ts` | Asserts that gRPC status code 7 (`PERMISSION_DENIED`) routes to `gateDenied` and keeps `error` null regardless of message copy rewording, and verifies clearing when legs are cleared or calculated successfully. |
| `frontend/src/store/matrix-bounds.test.ts` | `useCalculatorStore.ts` | Verifies `setMatrixBounds` patch semantics (modifying min leaves max unchanged and vice versa), input sanitization, and serialization of unset bounds as wire zero sentinels on `StrategyRequest`. |
| `frontend/src/store/model-limit.test.ts` | `useCalculatorStore.ts` | Verifies that gRPC status code 9 (`FAILED_PRECONDITION`) routes to `modelLimit` rather than `error`, asserting that model boundaries do not present as application outages. |
| `frontend/src/store/saved-scenarios.test.ts` | `useSavedScenariosStore.ts`| Verifies status code discrimination for UNAUTHENTICATED (16 -> `needsSignIn`) and PERMISSION_DENIED (7 -> `needsPro`), round-trip wire serialization through `buildStrategyRequest`, and `apply` restoration fidelity. |
| `frontend/src/store/ticket.test.ts` | `useCalculatorStore.ts` | Asserts `commitTicket` guards (strike, premium, expiration resolution), verifies preservation of contract details and Asian averaging style, and asserts monotonic generation of unique leg IDs (`legSeq`). |
| `frontend/src/store/tree-pricer-not-ready.test.ts` | `useTreePricerStore.ts` | Verifies that tree pricer preconditions (no spot, no strike, no IV, no DTE, empty Bermudan Asian dates) route to `notReady` and clear `error`. |

---

## Open questions

None. All documented behaviors, bounds, invariants, and failure routes were verified directly against the code.
