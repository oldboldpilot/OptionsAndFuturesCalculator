# Session Log: 2026-07-29

@author Olumuyiwa Oluwasanmi

Backend brought live on Railway behind the production custom domain, the NAS
backup repaired, and a real-data violation removed from the symbol selector.

## 1. Custom Domain `api.optionsandfuturescalculator.com` Brought Live

The backend container had been deploying and passing its healthcheck since
2026-07-27, but every request to the public API hostname returned Railway's
`{"status":"error","code":404,"message":"Application not found"}`. Three
separate faults were stacked; each had to be cleared in order.

**Fault 1 — the hostname was never registered on the Railway service.**
`railway domain` listed only the generated
`options-calculator-backend-production.up.railway.app`. The `x-railway-fallback: true`
response header is Railway's edge reporting that it received the request,
matched the `Host` header against its custom-domain table, and found no owner.

The CLI could not fix this: `railway domain <name>` returns `Unauthorized`
while `railway whoami`, `status`, `list` and `domain` (read) all succeed.
Cause is in `~/.railway/config.json` — `user.token` is `null` and only a
43-character `user.accessToken` is present, which carries read scope but not
account-level mutation scope. The `RAILWAY_TOKEN` in `config/.env` is rejected
as invalid. Registered instead via Railway's public GraphQL API:

```
mutation customDomainCreate(input: {
  domain: "api.optionsandfuturescalculator.com",
  projectId: 59242db9-5174-4722-aea3-4df2a8f54f99,
  environmentId: 273bbc5d-b59f-4d36-9ef4-69b5d79dabaf,
  serviceId: 0216e414-5dd6-42a5-be9a-b85b54d87fa2,
  targetPort: 8080 })
-> id 96746301-a1ac-4ceb-9b92-1f6733d84253
```

Note the endpoint is behind Cloudflare's bot protection and rejects requests
with a default `curl` User-Agent (HTTP 403, error code 1010). A browser
User-Agent is required.

**Fault 2 — the Cloudflare CNAME pointed at the wrong Railway hostname.**
The record targeted `options-calculator-backend-production.up.railway.app`,
the *service* domain. Attaching a custom domain makes Railway mint a
per-domain endpoint — here `3nw3v5qd.up.railway.app` — and its router accepts
the hostname only when the CNAME resolves to that. It was also proxied
(orange cloud), so Railway resolving the record saw Cloudflare's anycast
addresses and no CNAME at all, leaving `currentValue` empty and blocking both
verification and the ACME challenge. Corrected to
`3nw3v5qd.up.railway.app` with `proxied: false`.

**Fault 3 — the ownership verification TXT record was missing.**
`certificateStatus` sat at `CERTIFICATE_STATUS_TYPE_VALIDATING_OWNERSHIP` for
roughly forty minutes across two explicit `customDomainIssueCertificate`
calls. This reads as a stuck job but is not: the status object carries
`verified: false` alongside a populated `verificationDnsHost` and
`verificationToken`, while every `certificateError*` field is `null`. Null
errors are the diagnostic — a job that is failing populates
`certificateErrorType` and `certificateRetryable`. The state machine was
parked on a precondition. Created in Cloudflare:

```
TXT  _railway-verify.api.optionsandfuturescalculator.com
     railway-verify=8f82c9d1551c332e493fb88c771e8d217304e8d00549b9139f6a3fed1eb8a733
```

`verified` flipped to `true` and the certificate reached
`CERTIFICATE_STATUS_TYPE_VALID` within about forty seconds.

**Ruled out along the way,** so it is not re-investigated next time: no CAA
record blocks issuance. Because `api.` is a CNAME, RFC 8659 directs the CAA
lookup to the CNAME *target's* tree rather than this zone — the governing
records live on `railway.app`, which explicitly permits `letsencrypt.org`. An
empty CAA on `optionsandfuturescalculator.com` proves nothing here.

**Verified end to end:**

- Certificate `CN=api.optionsandfuturescalculator.com`, Let's Encrypt, valid
  to 2026-10-27.
- `POST /calculator.OptionsCalculator/GetMarketQuote` returns HTTP/2 200,
  `content-type: application/grpc-web+proto`, trailer `grpc-status:0`, and a
  live Alpaca SPY quote timestamped at request time.
- CORS preflight returns 200 with
  `access-control-allow-origin: https://optionsandfuturescalculator.com` and
  `x-grpc-web` among the allowed headers.

### Current DNS (not recorded in git anywhere else)

| Record | Target | Proxied | Serves |
| --- | --- | --- | --- |
| `optionsandfuturescalculator.com` | `optionsandfuturescalculator.pages.dev` | yes | UI (Cloudflare Pages) |
| `www.optionsandfuturescalculator.com` | `optionsandfuturescalculator.pages.dev` | yes | UI (Cloudflare Pages) |
| `api.optionsandfuturescalculator.com` | `3nw3v5qd.up.railway.app` | **no** | Backend (Railway) |
| `_railway-verify.api...` TXT | `railway-verify=8f82c9d1...` | n/a | Certificate renewal |

Deleting the TXT record or re-enabling the proxy on `api.` will break
certificate renewal in October 2026.

## 2. NAS Backup Repaired

`scripts/backup_to_nas.sh` had been failing on its final step on every run and
the failure was going unnoticed. The NAS is mounted over CIFS
(`//192.168.1.119/DataFolder`), which cannot store a POSIX symlink unless the
share is mounted with `mfsymlinks`. rsync therefore failed with `Operation not
supported (95)`, exited 23, and `set -e` aborted the script before its success
line — so the only symptom was the absence of a message, which is easy to miss
at the end of a multi-thousand-line rsync log.

The tree holds exactly one symlink,
`backend/sensen/external/CosyVoice/third_party/Matcha-TTS/data`, vendored
third-party code pointing at an absolute path on an upstream author's machine
and already dangling locally. Added `--no-links`, which skips symlinks while
still printing `skipping non-regular file` for each, so one that matters later
stays visible rather than being silently dropped. The script now exits 0.

## 3. Real-Data Violation Removed From `SymbolSelector.tsx`

Browser verification against the live site surfaced fabricated data on screen:
SPY quoted at `$580.00` against a real `$738`, a perfectly mirror-symmetric
option chain (call bid at 510 equal to put bid at 650, identical open interest
in both wings, delta exactly ±0.50 at the money), and static risk metrics.
Most of that is a stale Cloudflare Pages bundle (see item 4), but one cause
was live in current source.

`SymbolSelector.tsx` carried a hardcoded price for each preset instrument and
passed it as `setSymbol(match.symbol, match.price, match.category)`. The store
treats a defined `customPrice` as a deliberate user simulation override and
returns early *before* the quote fetch:

```ts
if (customPrice !== undefined) return;
```

So the fabricated price was not a placeholder that flashed before real data
arrived — it became the spot price for the remainder of the session, and every
payoff, Greek and probability was computed from it. This is exactly the class
of failure spec §3.4 forbids.

Fix: `price` removed from the `SymbolPreset` type and all 22 entries, from the
search dropdown, and from the preset pills (which now show asset class
instead). `applySymbol` passes `undefined`, so the real quote is always
fetched. Symbol, display name and asset class remain — those are reference
data about what an instrument *is*, not market observations.

The store itself was already correct and is unchanged: `spotPrice` initialises
to `0`, a successful quote overwrites it, a failed quote sets `0` with an error
rather than substituting a plausible number, and `calculateStrategy` refuses
when `spotPrice <= 0`. Verified no other hardcoded price table remains in
`frontend/src`. `npm run build` passes.

## 4. Greek Units Corrected At The Service Boundary

The strategy panel was displaying `Θ −21,251.836` and `V 3,931.162` for a
7-DTE call spread whose entire risk was $861. The values were **arithmetically
correct and conventionally mislabelled** — confirmed by reproducing all five
Greeks to ≤0.3% against the engine, and again independently by hand-computing
Black-Scholes for the same position and matching the rebuilt engine to four
decimals.

`sensen::price_black_scholes` (`backend/sensen/src/options.cppm:550-592`)
returns textbook derivatives: theta and charm are per **year** because `T` is in
years, and vega, vanna, volga and rho are per **1.00** of vol or rate. There is
no `/365` and no `/100` anywhere in the library. That is correct for a maths
library and was deliberately left alone — it is also a submodule with its own
review policy. The consuming service owns presentation units, so the conversion
went into `action_greeks` (`backend/src/modules/calculator_service.cpp:456-478`),
the single site that emits `net_greeks`, which makes double conversion
structurally impossible:

| Greek | Divisor | Displayed unit |
| --- | --- | --- |
| delta | — | position share-equivalents |
| gamma | — | delta per $1 of spot |
| theta | `/365` | $ per calendar day |
| vega | `/100` | $ per 1 IV point |
| rho | `/100` | $ per 1 rate point |
| vanna | `/100` | one vol factor |
| volga | `/10000` | two vol factors (∂vega/∂σ) |
| charm | `/365` | day count |

The proto now documents these units so the next client inherits the convention
rather than the trap, and `StrategyMetrics.tsx` renders an explicit unit beside
every label. Observed after the fix: `θ −33.03 $/day`, `V 82.92 $/1% IV`.

**A second, unrelated bug fell out of the same function.** `years` was computed
once *outside* the leg loop from `ctx->horizon`, which is a **max** over the
legs — so every leg was priced at the longest-dated leg's time to expiry. For a
same-strike calendar spread that meant identical `S, K, σ, T` with opposite
direction, and **every Greek cancelled to exactly zero**. Each leg now uses its
own `expiration_days`, falling back to the position horizon rather than to zero,
because sensen returns all-zero Greeks for `T <= 0` (`options.cppm:557-561`) and
a zero fallback would make the leg silently vanish from the aggregate instead of
being visibly absent.

## 5. Risk-Free Rate Now Measured Rather Than Invented

`riskFreeRate: 0.05` was hardcoded in the browser store, called by nothing, and
still shaped expected value, probability of profit and the whole distribution
curve. It is now observed.

New `GetRiskFreeRate` RPC, backed by `TreasuryParYieldProvider` — a **keyless**
CSV fetch of the US Treasury par yield curve from `home.treasury.gov`, behind a
`RateProvider` concept and registry mirroring the existing market-data provider
seam (`RISK_FREE_RATE_PROVIDER` selects, 6h cache, last-good fallback). Keyless
matters for a practical reason: Railway variable writes return `Not Authorized`
from this session, so a provider needing an API key could not have been
provisioned at all.

Its own RPC rather than a field on `QuoteResponse`, because the rate is a
property of the market, not of an instrument — a field would refetch a global
datum on every symbol change and attribute its provenance to whichever ticker
happened to ask.

**Failure is reported as failure.** There is no `0.0` sentinel: in a
`double rate` a zero cannot be distinguished from a genuine zero short rate,
which is a real historical observation. The gate is on `as_of_date`, not on the
value being positive, so a 0.00% print survives while an unusable response does
not. The client then states the rate is unavailable and lets the user supply one,
labelled `ASSUMPTION`.

The header chip carries tenor **and observation date** (`CMT 3M · 2026-07-29`)
and styles itself stale past four calendar days — the widest ordinary gap being
a Friday print read on a Tuesday after a Monday holiday. The backend serves its
last good print indefinitely when the feed is down, so without the date on
screen an arbitrarily old rate would have rendered as live.

### Deploy ordering is not optional

The browser refuses to calculate without a rate rather than substituting one.
So if the frontend ships first, `GetRiskFreeRate` returns `UNIMPLEMENTED`, no
route produces any result at all, **and** every Greek renders under a unit label
the old backend's values do not satisfy — strictly worse than the original bug,
because an unlabelled number became an explicitly false assertion.
**Railway first, or simultaneously. Never Cloudflare first.**

`smoke_client` was extended to cover the fourth RPC and assert a non-empty
`as_of_date` and curve. Without that a container unable to reach treasury.gov
would pass every check, deploy green, and serve a calculator that returns
nothing — the smoke test is the only place egress from the deployment
environment can be proven, since it always succeeds from a workstation.

## 6. Known Outstanding

- **The live UI is a stale Cloudflare Pages bundle.** The deployed JavaScript
  makes zero requests to `api.optionsandfuturescalculator.com` and still
  renders placeholders (`John Doe`, IV `24.5%`, VaR `$12,500`, Sharpe `1.80`)
  that no longer exist anywhere in source. Republishing
  `frontend/out` is required before the panels show real data; the endpoint
  configuration already points at the right host and needs no change.
- **`backend/sensen` carries an unfinished refactor, and the one fix of ours in
  it cannot be separated from it.** Earlier notes in this log claimed the
  submodule would not build from a clean recursive clone because
  `src/gp_ara_distributed.cpp` is untracked while `src/CMakeLists.txt:529`
  references it. That is wrong, and the correction matters: the *committed*
  `src/CMakeLists.txt` references only `gp_ara_distributed.cppm`. The two `.cpp`
  references at lines 529 and 546 are themselves uncommitted, so a clean
  recursive clone never looks for the missing file and builds normally. The
  breakage exists only in this working tree.

  What is actually in flight there is a module-partition split: the working tree
  guts `gp_ara_distributed.cppm` by 989 lines and moves the implementation into
  the new untracked `gp_ara_distributed.cpp`, with CMakeLists updated to compile
  it. Our `std::views::filter` → direct-iteration fix (libstdc++ 14 rejects the
  pipe over that const range; 15 accepts) lives *inside* that new file, so
  committing the fix would mean committing the entire refactor.

  Both submodules are therefore left untouched: `sensen` is on a detached HEAD
  with 26 modified tracked paths, 3 untracked, and dirty nested submodules
  (`external/cpp23-logger`, `external/fastestjsoninthewest`); `backend/external/SGEE`
  has 98. Almost none of it is this session's work, an in-progress TBB →
  `sensen::parallel` migration runs through it, and each carries its own review
  policy. Nothing in the parent repository depends on any of it being committed:
  the Greeks deployed here come from committed sensen code — the uncommitted
  `options.cppm` diff touches only the Monte Carlo Asian-option path, not
  `price_black_scholes`.
- Futures term-structure panel and the 3D P&L surface from the approved scope.
- **Calendar-spread P&L metrics are wrong**, and now conspicuously so. `value_at`
  (`calculator_service.cpp:99`) prices every leg on one clock, so SELL 730 @7d /
  BUY 730 @60d returns `max_profit = max_loss = −1700.00`. The near leg should
  expire partway along the time axis and be carried at intrinsic thereafter.
  Deliberately out of scope here — it changes the whole matrix, the expiry curve
  and every metric derived from them — but this change set makes a calendar
  spread's *Greeks* correct while its max profit, max loss, expected value and
  PoP stay wrong, so a user now has less reason to distrust the panel than
  before. Should be the next change set. No calendar entry was found in the
  frontend strategy catalogue, so user-facing reachability is unconfirmed.
- **`-ffast-math` in the build tree violates policy rules 50/55.**
  `backend/build/build.ninja:32030,32141,32151` compile parts of this tree with
  `-ffast-math -march=native -mtune=native`. Pre-existing and unrelated to this
  work, but it is a real finding rather than the false positive it was first
  taken for.
- The intra-proto Greek convention split remains: `ChainResponse` per-strike
  Greeks pass through verbatim from Alpaca (per-share, per-day theta) while
  `StrategyResponse.net_greeks` is position-scaled. Both are now documented in
  the proto, but they are still two conventions in one message set.
- No dividend yield in the Black-Scholes model (`options.cppm:562-588`), so
  SPY's ~1.2% yield is unmodelled and call deltas and rho are biased slightly
  high. `StrategyRequest.dividend_yield` exists on the wire and is unconsumed.
