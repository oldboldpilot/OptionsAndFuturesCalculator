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

## 4. Known Outstanding

- **The live UI is a stale Cloudflare Pages bundle.** The deployed JavaScript
  makes zero requests to `api.optionsandfuturescalculator.com` and still
  renders placeholders (`John Doe`, IV `24.5%`, VaR `$12,500`, Sharpe `1.80`)
  that no longer exist anywhere in source. Republishing
  `frontend/out` is required before the panels show real data; the endpoint
  configuration already points at the right host and needs no change.
- **`backend/sensen` does not build from a clean recursive clone.**
  `src/gp_ara_distributed.cpp` is untracked in the submodule yet referenced by
  `sensen/src/CMakeLists.txt:529`. The `std::views::filter` fix applied to it
  (libstdc++ 14 rejects the pipe over that const range; 15 accepts) is also
  uncommitted. Left alone: that submodule has 29 modified paths and
  `backend/external/SGEE` has 98, almost none of this session's work, and it
  carries its own review policy.
- Futures term-structure panel and the 3D P&L surface from the approved scope.
- `theta` / `vega` conventions need confirming before display — sensen appears
  to return per-year and per-1.00-vol rather than per-day and per-1%.
