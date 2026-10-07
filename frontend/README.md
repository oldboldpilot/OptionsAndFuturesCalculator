# Options & Futures Calculator frontend

The web frontend for optionsandfuturescalculator.com: multi-leg option and futures strategies priced on live market data, with payoff curves, Greeks, option chains, a futures term structure, a P&L matrix and surface, saved scenarios, a natural-language strategy assistant, and written strategy guides.

## 1. Architecture

- **Next.js 16 (App Router), static export.** `next.config.ts` sets `output: "export"`, so the build produces plain HTML, JS and CSS in `out/`.
- **Cloudflare Workers static assets.** `wrangler.toml` serves `./out` through the `ASSETS` binding (`not_found_handling = "404-page"`, `html_handling = "auto-trailing-slash"`). This is a Workers deployment, not Cloudflare Pages.
- **Auth proxy.** `worker.ts` is the Worker entry point. It forwards `/auth/v1/*` to the self-hosted GoTrue instance named by the `GOTRUE_ORIGIN` variable in `wrangler.toml` (so browser sign-in is same-origin) and hands every other request to the static assets.
- **Backend calls.** The pages talk to the C++ engine over gRPC-Web (`grpc-web`, `google-protobuf`) at `NEXT_PUBLIC_API_URL`. The generated clients live in `src/grpc/` and are produced by `scripts/gen_proto.sh` from `backend/proto/`; regenerate them with that script, never by hand.
- **State.** Zustand stores in `src/store/`: `useCalculatorStore` (the position and its priced results), `useTreePricerStore` (tree pricing), `useAssistantStore` (the strategy assistant, `ParseStrategy`) and `useSavedScenariosStore` (`SaveStrategy`, `ListStrategies`, `DeleteStrategy`).

## 2. Routes (`src/app/`)

| Route | Purpose |
| --- | --- |
| `/` | The calculator workspace, with the written site guide below it. |
| `/calculator/[strategy]` | The calculator for one strategy. The 26 slugs are `STRATEGY_SLUGS` in `src/config/strategies.ts` (for example `/calculator/iron-condor`). |
| `/guides` | Index of the strategy guides. |
| `/guides/[strategy]` | One written guide per slug, from `src/content/strategy-guides.ts`. These are the only pages that carry Google ad code. |
| `/widget` | An embeddable calculator view (not indexed). `?theme=light` selects the light theme, anything else the `slate` theme; `?symbol=XYZ` sets the underlying, upper-cased. |
| `/auth/sign-in`, `/auth/reset-password` | Account sign-in and password reset. |
| `/privacy`, `/terms` | Policy pages. |

`not-found.tsx`, `robots.ts` and `sitemap.ts` supply the 404 page, `robots.txt` and `sitemap.xml`.

## 3. Calculator panels (`src/components/`)

- `StrategyWorkspace` lays out the calculator; `StrategySelector` picks a strategy preset (categories: Bullish, Bearish, Neutral, Volatility, Income & Hedge, Futures).
- `OptionTicket` and `PositionLegs` build and edit the legs; strike, quantity and premium stay editable.
- `ExerciseStylePanel` and `BermudanDateBuilder` choose European, American or Bermudan exercise and Asian averaging (average price or average strike).
- `OptionChain` shows the live chain; `TermStructure` shows the futures forward curve for futures symbols.
- `ProbabilityCurve` shows the payoff and probability distribution; `PayoffLadder` lists the payoff at the curve date row by row; `PnLMatrix` is the price by date grid (the price axis can be pinned with `matrix_price_min` / `matrix_price_max`); `PnLSurface` is the same data as a 3D surface (Three.js through React Three Fiber).
- `StrategyMetrics` summarises max profit, max loss, breakevens and Greeks.
- `SavedScenarios` saves and reopens named positions for a signed-in account.
- `AssistantPanel` turns a plain-English description into a position through the strategy assistant.
- `TopBar` shows the symbol, live spot and data provenance; `SiteNav` is the Calculator | Guides tab bar; `ThemeToggle` switches between the `slate` and `light` themes, remembered in `localStorage` under `ofc-theme`; `CurrencySelect` picks the display currency.
- `ProPanel` and `UpgradePrompt` show subscription state and the upgrade path; `SponsoredBrokers` renders this site's own broker links.

The chain's LIVE or DELAYED badge is derived from the chain's `fetched_at` timestamp by `src/lib/chainFreshness.ts`, not from request status.

## 4. Pro gate

Multi-leg strategies (more than one leg), saved scenarios and the strategy assistant need Pro; a single-leg strategy is free. The engine decides: this bundle is a static export the user already has, so nothing here enforces anything. `src/lib/licence.ts` keeps the subscriber's signed licence in `localStorage` and `src/lib/useProStatus.ts` reads the Pro tier from the licence or from the signed-in account's `app_metadata.tier`, only to label the UI honestly. Neither verifies a signature in the browser. Every gRPC call carries the licence or the account token (`authMetadata()` in `licence.ts`), and when neither is present it sends the site's publishable key (`NEXT_PUBLIC_FINANCE_API_KEY`) as `x-api-key`. A pricing refusal with gRPC status 7 renders as `UpgradePrompt`; for saved scenarios, status 16 offers sign-in and status 7 offers the upgrade.

## 5. Money formatting (`src/lib/currency.ts`)

Amounts are formatted and parsed in one place so grouping and decimal separators follow the display locale: `formatMoney`, `formatAmount` and `formatMoneyCompact` for output, and `parseMoneyInput`, `formatMoneyInput` and `separatorsFor` for the grouped text inputs (`MoneyInput`). The currency picker changes the symbol only; nothing converts the dollar amounts.

## 6. Environment variables

Read at build time (the app is a static export) unless noted.

| Variable | Used for |
| --- | --- |
| `NEXT_PUBLIC_API_URL` | gRPC-Web base URL; defaults to `https://api.optionsandfuturescalculator.com`. |
| `NEXT_PUBLIC_SUPABASE_URL`, `NEXT_PUBLIC_SUPABASE_ANON_KEY` | Auth client; placeholders are used when unset, so sign-in is not configured. |
| `NEXT_PUBLIC_FINANCE_API_KEY` | The site's publishable API key, sent as `x-api-key` when there is no licence or account token. When unset no header is sent. |
| `NEXT_PUBLIC_BILLING_URL` | Billing service that starts Stripe Checkout for the upgrade flow (`startCheckout` in `src/lib/licence.ts`); has a built-in default. |
| `NEXT_PUBLIC_OAUTH_ENABLED` | Set to `1` to show the OAuth buttons on the sign-in form. |
| `NEXT_PUBLIC_ADSENSE_SLOT_LEADERBOARD`, `_RECTANGLE`, `_MULTIPLEX`, `_IN_ARTICLE` | AdSense slot ids for `AdSlot`. The multiplex and in-article slots have built-in defaults; a slot with no id does not request an ad. |
| `NEXT_PUBLIC_COMPANY_NAME`, `_APP_NAME`, `_APP_DESCRIPTION`, `_THEME_COLOR`, `_LOGO_URL`, `_OG_IMAGE_URL`, `_TWITTER_HANDLE`, `_CANONICAL_URL` | Site branding and metadata (`src/config/branding.ts`), each with a default. |
| `GOTRUE_ORIGIN` | Runtime variable of the Worker, set in `wrangler.toml`; the upstream for `/auth/v1/*`. |
| `BASE_URL` | Playwright only: run the end-to-end tests against an already running site. |

## 7. Commands

```bash
npm run dev               # development server on http://localhost:3000
npm test                  # unit tests (Vitest, single run)
npm run test:watch        # Vitest in watch mode
npm run test:e2e          # Playwright end-to-end tests
npm run build             # static export to out/, then check-export and check-indexability
npm run check:export      # re-run the export checks on out/
npm run check:indexability
npx wrangler deploy       # publish out/ and worker.ts to Cloudflare Workers
```

`scripts/check-export.mjs` inspects the emitted HTML in `out/` (ad code only on the 26 guide articles, among other checks), so a build that passes `next build` can still fail `npm run build`.
