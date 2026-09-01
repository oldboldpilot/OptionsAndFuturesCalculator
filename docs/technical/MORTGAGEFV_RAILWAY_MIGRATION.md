# mortgagefvcalculator.com: off Lovable, onto Railway

@author Olumuyiwa Oluwasanmi

Completed 2026-09-01. The site now runs entirely on our own infrastructure:
Cloudflare in front, Railway behind, with the finance engine reached over
native gRPC on the private network.

---

## What the measurements changed

The migration runbook was written by reading the code. Running it against
reality removed most of the work.

| the runbook said | measured | effect |
| --- | --- | --- |
| request a DB export from Lovable support; "it gates the whole schedule" | export in hand: **24 data rows, 1 user** | no external dependency at all |
| §5.3 replace the Lovable AI Gateway with our own model provider | `ai-gateway.server.ts` is imported by **nothing** | delete it; there was never an external LLM |
| a Stripe account move would break live subscriptions | already the owner's account; `subscriptions` **empty** | new keys and a webhook URL, nothing more |
| §3.2 lists **13** tables | the exporter covers **16** | verify against 16 |
| §3.4 "Google provider enabled" | live config: **apple, google, email** | and none of them had credentials — see below |
| §4.1/§5.6 replace vite.config.ts, delete `.lovable/` | `NITRO_PRESET` overrides the preset with the wrapper intact | **keep** Lovable as the design tool |

**The AI was already ours.** `chat.ts` calls `ParseOperation` on our fine-tuned
Qwen3 in the C++ engine, a local deterministic fallback parser, and our own
finance service. The "AI gateway" was dead code documenting a dependency that
did not exist.

**Lovable was lending its OAuth registrations.** The live Supabase project
answers `400 "Unsupported provider: missing OAuth secret"` for both google and
apple: social login never touched Supabase's OAuth, it went through
`@lovable.dev/cloud-auth-js` against *Lovable's* registered apps. That is a real
service they provided and the one thing with no technical substitute — Google
and Apple issue client IDs to a legal identity. Resolved by dropping social
login entirely: GoTrue does email and password natively, with no third party,
no fees and no consent screen.

---

## Architecture

```
                    visitors
                       │
                 Cloudflare            DNS · TLS · CDN · WAF
                       │
                 mfv-web  (TanStack Start, nitro node-server)
                    │        │
   supabase-shaped  │        │  native gRPC, private network
    gateway (Caddy) │        │
      ┌─────────────┘        └──────────────┐
   mfv-auth (GoTrue)                options-calculator-backend
   mfv-postgrest                      :50051  (C++ engine + the
      └──── Postgres / mortgagefv ────┘        mortgage assistant)
```

`mfv-gateway` exists because `supabase-js` takes ONE base URL and derives
`/auth/v1` and `/rest/v1` from it. Self-hosted those are two services; nine
lines of Caddy present them as one origin. Supabase's own stack uses Kong for
the same job.

---

## Native gRPC, and why it is possible here and nowhere else

Native gRPC carries `grpc-status` in HTTP/2 **trailers**, and Railway's public
edge drops trailers — so a native call through
`api.optionsandfuturescalculator.com` reaches the engine, gets the right
answer, and dies on the way back with `Stream removed`. gRPC-Web exists to
route around exactly that, by moving trailers into the body.

`options-calculator-backend.railway.internal:50051` has no such edge in the
path. Verified before a line was written: the port accepts an HTTP/2 preface
and answers with a SETTINGS frame.

Measured, same question, same answer (`1,798.65`) on all three:

| transport | latency |
| --- | --- |
| public HTTPS + JSON | 0.11 – 1.97 s |
| private + gRPC-Web binary | 0.10 – 0.30 s |
| **private + native gRPC** | **0.073 – 0.100 s** |

**The transport is chosen by the ADDRESS, not by a flag.** `isPrivateNetwork()`
reads the host, so the fast path cannot be selected where it cannot work. A flag
would let a laptop or a preview deployment pick native gRPC and fail on every
request.

Two details are load-bearing. **One HTTP/2 session per origin, reused** — a
fresh session costs a round trip per call, which would make native gRPC
measurably *slower* than the JSON it replaces, and is the trap in every naive
benchmark of the two. And **no body plus no status is an error, not an empty
answer** — that is the trailer-stripping signature itself.

---

## Four defects found, three of them mine

**The transport default contradicted its own comment.** `finance-api.server.ts`
read `|| "grpc-web"` under a comment saying *"Default stays JSON until the
binary path has soaked in production"*. protobufjs builds codecs with
`new Function()`, **Cloudflare Workers prohibit dynamic code generation**, so
every finance call inside the Worker threw `Code generation from strings
disallowed`. Users saw "This estimate could not be calculated"; SSR emitted 200s
with no figures. Not a CSP problem — no header changes it.

**A leading space took down every page.** `config/.env` held
`STRIPE_PUBLISHABLE_KEY= pk_live_…`. `stripe.ts` tests
`clientToken?.startsWith("pk_live_")`, which was false, and the app threw
*"Payments are not configured for this build"* at the **root** error boundary —
so a Stripe check broke pages with no payments on them. One character.

**A database-wide `search_path` broke GoTrue.** Setting
`search_path = public, extensions` to satisfy the app's unqualified
`gen_random_bytes()` also redirected GoTrue's own bookkeeping table into
`public`. It then found `auth.schema_migrations` empty on every boot, replayed
27 migrations, and died on one predating the `identities.id` type change. Scope
`search_path` to the app's roles; let GoTrue own the `auth` schema entirely.

**The webhook secret belonged to a different endpoint.** `PAYMENTS_LIVE_WEBHOOK_SECRET`
was taken from `config/.env`, whose `STRIPE_WEBHOOK_SECRET` is another app's on
the same shared account. Signature verification would have rejected every
delivery and subscriptions would silently never activate.

---

## Things that cost time and are worth not repeating

**Do not judge a client-rendered page from `curl`.** Most of these pages compute
after hydration, so "no figures in the SSR HTML" is the healthy state. Several
rounds were spent inferring breakage from output that could never have shown it.
A real browser found the actual error in one run — that should be the first
move, not the eighth.

**Railway reports SUCCESS while crash-looping.** `mfv-auth`'s first green deploy
had **11** `uuid = text` errors in its log. The healthcheck passes before the
crash. Read the logs, not the status.

**One `railway.json` at a repo root speaks for one service.** A gateway service
pointed at this repo spent its build compiling the C++ engine. `rootDirectory`
does not override it; `railwayConfigFile` is deprecated. Put the service's files
in a repo without a competing root config, or upload the directory directly.

**Two lockfiles mean an edit through either tool is half an edit.** Removing
dependencies with npm updated `package-lock.json` and left `bun.lock` stale;
`bun install --frozen-lockfile` then refused the build.

**A 500 under a parallel sweep may be your own load test.** One URL in 536
failed at 12-way concurrency and passed on retry — Envoy allows 10 req/s per
replica.

---

## Final state

536/536 sitemap URLs at 200 on the live domain. Charts render, calculators
compute, all five embed widgets serve, `widget.js` already pointed at the
production domain so embeds on third-party sites followed the cutover by
themselves. The assistant answers `1,798.65` through native gRPC. Auth is email
and password with confirmation mail via Resend. Six security headers at parity
with the old deployment, canonical tags on the production origin, RLS proven to
FILTER rather than merely be enabled.

**Kept deliberately:** `vite.config.ts`, `.lovable/`, `AGENTS.md` and
`@lovable.dev/vite-tanstack-config`. Lovable remains the design tool; only the
hosting and the gateways left. `NITRO_PRESET=node-server` retargets the build
with their config byte-for-byte unchanged — their own wrapper documents that
`NITRO_PRESET` "still wins, so a self-deploy auto-targets its own platform".

**Rollback**, for the week the old deployment is kept: revert DNS. The old
Supabase is frozen the moment traffic moves, so nothing accumulates there that
would need replaying.
