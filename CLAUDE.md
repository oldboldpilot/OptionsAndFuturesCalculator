# Options & Futures Profit Calculator - Master Architecture Guide

@author Olumuyiwa Oluwasanmi

This document outlines the system architecture, build commands, and deployment infrastructure for the Options & Futures Profit Calculator.

## System Architecture

```
[ Client Browser ]
      │
      ├──> Cloudflare Workers static assets (Frontend Web UI: Static Next.js Export)
      │      • Domain: https://optionsandfuturescalculator.com
      │      • WWW Alias: https://www.optionsandfuturescalculator.com
      │      • Verification URL: https://optionsandfuturescalculator.muyiwamc2.workers.dev
      │      • NOT Cloudflare Pages -- the Pages project still exists and still
      │        deploys, but NOTHING points at it. See Deployment Details.
      │
      ├──> Railway Container Ingress (gRPC-Web / HTTP Engine Proxy)
      │      • Public Custom Domain: https://api.optionsandfuturescalculator.com
      │      • Railway Service: options-calculator-backend
      │      • Engine: Native C++23 Options & Futures Engine + Envoy Proxy (Port 8080)
      │
      └──> Railway PostgreSQL Database
             • Internal Host: postgres.railway.internal:5432
             • Public TCP Proxy: monorail.proxy.rlwy.net:11453
             • Database: railway (Tables: users, profiles, saved_strategies)
```

## gRPC Surface

The engine serves **four** services on one port (`:50051` native, and through
Envoy's catch-all route as gRPC-Web). They are separate contracts, not separate
deployments. The route is a catch-all prefix precisely so a new service on this
port needs no proxy change — but each one must still be added to Envoy's
`grpc_json_transcoder` services list, which resolves methods by name.

| Service | Proto | Purpose |
| --- | --- | --- |
| `calculator.OptionsCalculator` | `backend/proto/calculator.proto` | This application's own API — strategies, legs, payoff curves, market data |
| `sensen.finance.Finance` | `backend/proto/finance.proto` | The general-purpose sensen financial library, exposed for reuse by other applications |
| `calculator.assistant.StrategyAssistant` | `backend/proto/assistant.proto` | Natural-language strategy parsing, served by a fine-tuned Qwen3-0.6B running in-process |
| `mortgage.assistant.MortgageAssistant` | `backend/proto/mortgage_assistant.proto` | Natural-language MORTGAGE / time-value-of-money parsing, served by a SECOND, different fine-tuned Qwen3-0.6B in the same process |

`sensen.finance.Finance` covers roughly fifty functions across sensen's
`financial.cppm`, `options.cppm` and `portfolio.cppm`: time value of money,
mortgage amortization (with tax deductions), HELOC, rental ROI, NPV/IRR/XIRR,
depreciation, bonds, T-bills, futures pricing and margin simulation, hedging,
commodity spreads, option pricing (trees, Black-Scholes with full Greeks, Monte
Carlo) and portfolio statistics/optimization.

**Numeric types are not uniform, and the difference is deliberate.** A field is
`string` where sensen computes in `BigDecimal` (an exact `__int128` fixed-point
decimal, eighteen places) and `double` where sensen genuinely computes in
`double`. Money is a decimal string because rounding it to `double` compounds
over a 360-period amortization, and because this service is reachable from
browsers where JavaScript's `number` *is* a float64 — a `double` money field is
lossy on the client before anyone writes a line of code.

Gate: `backend/src/smoke_client.cpp` (`check_finance`) verifies the answers
against independent identities — put-call parity, price/yield inversion,
schedule closure, and the closed-form annuity formula — not against figures the
engine produced earlier.

**Native gRPC does not survive the Railway ingress.** `smoke_client` against
`api.optionsandfuturescalculator.com:443` fails with `Stream removed`, and no
corresponding request appears in `railway logs` — only gRPC-Web reaches the
container. Verify production through the browser path or the logs; the native
smoke client works locally and against the Railway TCP proxy, not the custom
domain.

## Strategy assistant

A fine-tuned Qwen3-0.6B (QLoRA, rank 16, 95.0% params exact-match) converts a
plain-English request into calculator parameters. It runs **in-process**, Q8_0
on CPU, fetched at image build time with the checksum pinned — it cannot travel
through `railway up`, which enforces an upload deadline that 62 MB already
failed.

**Where it is fetched FROM is currently unsettled, and no model is in the
deployed container.** The private HuggingFace repository that `MODEL_URL` pointed
at was **deleted on 2026-08-05** at the owner's instruction: the weights are
proprietary trade secrets and do not belong on a third-party registry, private or
not. This applies to both assistants' models. Consequences to hold on to:

- Nothing in this repo hosts a model, and `**/*.gguf` is gitignored, so a
  container built today has **no weights**. Each assistant answers
  `MODEL_UNAVAILABLE` and every other service is unaffected — that is the
  supported empty-`MODEL_URL` build, not a broken one.
- A replacement hosting mechanism **is being designed** and is deliberately not
  described here. Do not infer one from the old procedure, and do not re-upload
  to any model registry to "restore" the fetch.
- `backend/Dockerfile` gained a `backend/models/` build-context staging path so a
  LOCAL `docker build` can use a locally held GGUF. It is a stopgap. It does not
  help Railway, and the `!backend/models/*.gguf` exceptions in the root
  `.dockerignore` / `.railwayignore` should be expected to come out.
- The invariant that survives whatever replaces the transport: a model is
  checksum-verified before anything uses it, staging is not verification, and the
  checksum that counts is round-tripped from wherever the bytes are actually
  served.

`docs/MORTGAGE_MODEL_DISTRIBUTION.md` carries the same status banner and the
checksums; its HF publishing steps are marked superseded.

The full training-to-serving chain — dataset generation, the QLoRA-vs-full
comparison that decided the recipe, merge/export/quantize, the Dockerfile's
build-time fetch (and its secret-mount trap on Railway's builder), and every
serving constraint below in more detail — is documented end to end in
`docs/STRATEGY_ASSISTANT_PIPELINE.md`. This section is the short version.

The model serving today is pinned by `MODEL_SHA256` and its provenance —
recipe, hyperparameters, losses, wall clock, checksum, holdout score — is
§2b "Model of record" of that document; §4 has the procedure for swapping it.
It was trained 2026-08-03 from `unsloth/Qwen3-0.6B`, QLoRA rank 16, **4 epochs**
(`run_qlora.sh` overrides `train.py`'s `default=2.0`; reading that default as the
recipe produced a 2-epoch retrain scoring 5/16 against this model's 16/16).

**sensen is the standard for serving AND for conversion. llama.cpp is a
debugging and cross-checking tool only.** Its one legitimate role is being an
INDEPENDENT implementation for the parity probes (`backend/src/*_probe.cpp`) —
a reference that shared sensen's code would prove nothing. It is not a serving
alternative and not a conversion step:

- **Serving:** `ASSISTANT_BACKEND` defaults to `sensen` and production runs it.
  The production image is built `-DENABLE_LLAMACPP_BACKEND=OFF` (2026-08-04), so
  `ASSISTANT_BACKEND=llamacpp` there logs a build-configuration error and leaves
  the assistant unavailable rather than quietly substituting an engine. Every
  gate in this repo — the defect holdout, the baselines, the 95.0% params bar —
  is defined on the sensen path, so serving on llama.cpp would put production on
  an engine no gate covers.
- **Conversion:** `scripts/convert_to_gguf.sh` uses
  `sensen::convert_safetensors_to_gguf`, which writes Q8_0 directly from merged
  safetensors — one call, no f16 intermediate, **0.765 s** against a llama.cpp
  two-step that took minutes. Both score 16/16; sensen is the standard.
  `sensen.model_converter` is a universal hub (safetensors ↔ GGUF ↔ PyTorch ↔
  ONNX ↔ sensen-native), not just this one direction.
- **Evaluation:** measure candidates on sensen, never llama.cpp — a `llama-cli`
  score describes an engine that never handles a request. See
  `docs/guides/ASSISTANT_EVALUATION.md`.

Reaching for llama.cpp out of habit cost real work on 2026-08-03: it produced a
phantom regression that triggered an unnecessary retrain, and a conversion step
orders of magnitude slower than the one already in the tree.

Four things about it are load-bearing and easy to get wrong:

1. **The model requires its training system prompt.** Without it, it reverts to
   stock Qwen3 and emits no `<params>` block at all.
2. **Qwen3 emits a `<think>` block on every response, including correct ones.**
   The block being *empty* is the signal the system prompt took. Treating its
   presence as failure rejects every valid answer.
3. **`n_gpu_layers` must be 0 on a CPU build.** `GenerationConfig` defaults
   `compute_backend` to `AUTO`, which sensen counted as a GPU request; that
   enables `on_device_sampling`, a contract only the CUDA decode block honours,
   and the CPU path then casts a raw float logit to a token id. Fixed upstream
   in sensen and independently here, so it does not depend on the pinned commit.
4. **`repetition_penalty` must be 1.0, and "greedy" does not exempt you.**
   `sensen::Sampler::sample` applies the penalty to the logits **before** the
   greedy argmax, so `GREEDY` is argmax of *penalised* logits, not of the
   model's own. `GenerationConfig` defaults the penalty to **1.1** over a
   **64-token** window (`llm_interfaces.cppm:104,107`), so a caller who never
   asked for a penalty gets one on every decode. Worse, sensen's
   `applyRepetitionPenalty` compounded it **per occurrence** — a token seen *k*
   times in the window was divided by `penalty^k` — where HuggingFace and
   llama.cpp both deduplicate and penalise once per **unique** token. Both
   services now pin `config.repetition_penalty = 1.0F`
   (`assistant_service.cpp:660`, `mortgage_assistant_service.cpp:586`) as
   defence in depth even though the library is fixed, for the same reason the
   `n_gpu_layers = 0` line above it exists.

The per-occurrence compounding is fixed in sensen as of `fb2723cd`, but it is
recorded here because of how it presented — as everything except a sampler bug.

It is invisible on prose and lethal on JSON. Qwen3 tokenises **one digit per
token**, so on digit-dense structured output ASCII `'0'` lands in a 64-token
window 8–12 times, and `1.1^7 = 1.95` nearly halves that token's logit — more
than enough to drop the model's own top choice below a rival. Measured purely by
moving the penalty from 1.1 to 1.0, on two independent harnesses: params
exact-match **0/90 → 25/90** and **0/120 → 33/120**. Across 120/120 rows there
were **1,272** decode steps where the sampler disagreed with the raw argmax at
1.1, and **zero** at 1.0.

It also emitted **fullwidth zero (U+FF10)** inside numeric literals — a
*different token id* that escaped the penalty while ASCII `'0'` was being crushed
by `1.1^7`. 2/100 and 4/120 rows before the fix, **0** after. Mojibake in a
number reads exactly like decode corruption or a bad GGUF quantisation, and it
was neither: an independent llama.cpp rollout on the same GGUF agreed with
sensen at 8/30 in both directions. If you see a wrong digit or a strange
codepoint in generated JSON, **check the sampler before you blame the weights,
the quantisation, or the KV cache.**

**Greedy staying penalisable is a design choice, not the bug.** HuggingFace runs
its `LogitsProcessorList` before the greedy argmax and llama.cpp runs its
penalty sampler before its greedy sampler; sensen's ordering matches both. The
trap was the **1.1 default**, not the ordering — do not "fix" this by moving the
penalty after the argmax.

Two earlier worries about this fix were checked and are closed:

- **The DiffusionGemma concern is provably moot.** Diffusion sampling never
  reaches this code: it goes through `dlm::EntropyBoundSampler` /
  `sensen_cuda_eb_sample`, and `dlm.cppm` imports only `std`,
  `sensen.diffusion_core`, `sensen.cpu_features` and `sensen.parallel` — it
  cannot even name `Sampler`. (Its `MaskedUnmaskSampler` is its own type.) The
  chronology settles it independently: the penalty was introduced in `ad60b068`
  (2026-01-29) and the diffusion stack did not exist until 2026-06-11.
- **The options assistant's defect holdout is penalty-independent.** A/B on the
  same binary and the same model scores **6/14 params + 2/2 non-params at both
  1.1 and 1.0**. The penalty fix did not regress it, and its failures are the
  already-documented bare-futures-directive rows (`Long NQ`, `Short GC`, `gold
  outright long`). Its documented 13/16 differs from 6/14 by **harness
  denominator, not lost capability** — which is the whole reason every score in
  this file names its harness.

**Residual, untested:** the mirrored CUDA kernel `apply_rep_penalty_batch_kernel`
(`backend/sensen/src/cuda/cuda_llm_prefill.cu`) received the same dedup fix and
has been **compiled by nobody** — this machine builds `ENABLE_CUDA=OFF` and has
no `nvcc`. Every number above is CPU. The device-parity gates
(`test_gpu_batched_decode` Test 7, `test_gpu_argmax_softcap`) must run on a CUDA
build before that half is trusted.

Concurrency comes from sensen's iteration-level scheduler, not threads —
`generate()` cannot be called concurrently, because `FeedForwardNetwork` holds
`mutable` scratch per instance rather than `thread_local`, so parallel calls
corrupt hidden state silently. One owner thread, one fused `forwardBatch` per
step, ~20 MiB marginal per user.

Known limit: the training set restricts futures roots to `ES` and `NQ`, so
commodity queries are out of distribution — the model answers `CND` for a crude
crack spread, which is not an instrument. Symbol validation refuses that, and
`crack_321` is gated out of the UI (the calculator prices it correctly; the
assistant was never taught it). The deployed model also emits no `<params>` for a
bare futures directive (`Long NQ, 45 days, 2 contracts.`, `Short GC, ...`,
`gold outright long, ...`) — 3 of the 16 defect-holdout rows, measured 2026-08-03.

**Measure a candidate model on sensen, never llama.cpp** — see
`docs/guides/ASSISTANT_EVALUATION.md`. `ASSISTANT_BACKEND` defaults to `sensen`
and production runs it, so a `llama-cli` score describes an engine that never
handles a request. This is not theoretical: a `llama-cli` holdout scored the
deployed model 7/16 and triggered a retrain to fix a regression that did not
exist. Measured through the real RPC the same model scores **13/16**; the retrain
built to fix the phantom scored **5/16** and was not deployed.

Two traps make a bad measurement look like a model bug. Engines bind `:50051`
with `SO_REUSEPORT`, so several stale engines each holding a DIFFERENT model all
listen at once and the kernel splits requests between them — this produced a SPY
prompt answered with bond-futures text, which reads exactly like KV-cache bleed.
Assert `pgrep -x calculator_engi | wc -l` is 1 before trusting a number (the
`comm` name is truncated to 15 chars, so `pkill -x calculator_engine` matches
nothing). And symbol verification needs live market data; without it every model
scores identically because the refusal comes from the verification layer, not the
model — score `[assistant] raw model output`, which is logged before it runs.

## Mortgage assistant

A **second, different** fine-tuned Qwen3-0.6B, serving
`mortgage.assistant.MortgageAssistant` in the same process. It turns a
plain-English mortgage / time-value-of-money request into **the name of a
`sensen.finance.Finance` RPC plus that RPC's own parameters**. It computes
nothing; the Finance service runs the operation it names.

It reads `MORTGAGE_MODEL_PATH` and deliberately does **not** fall back to
`MODEL_PATH` — that names the STRATEGY weights, and loading them here puts a
model trained on option spreads behind a mortgage contract. One model, both, or
neither is a supported image.

Two things to hold on to before trusting its output:

- **It measures 27.8% params exact-match (25/90) through the real RPC**, against
  the strategy model's 95.0%. **Quote that number, not `evaluate.py`'s.**
  `agent/train/evaluate.py` scores **transformers on the merged bf16
  intermediate** — an artefact that never ships — and reports 31.7% (59/186) for
  the same weights. What ships is the Q8_0 GGUF on sensen, and that is the 27.8%.
  An independent llama.cpp rollout on the same GGUF puts the raw-decode ceiling
  at 8/30, agreeing with sensen. Three harnesses, three denominators: cite which
  one produced a figure or the figure means nothing. This project has already
  eaten one unnecessary retrain from trusting the wrong harness (see the
  strategy-assistant `llama-cli` phantom above); the bf16-vs-Q8_0 gap is the
  same mistake wearing different clothes.

  That 25/90 is **post-`repetition_penalty` fix**. The same model on the same
  harness scored **0/90** before it — see item 4 of the strategy-assistant list
  above. A near-zero RPC score against a non-zero `evaluate.py` score is the
  signature of a serving-path defect, not a bad checkpoint; do not retrain on
  one.

  Its dangerous failure is a CORRUPTED VALUE — it emitted `5379.00` for a stated
  `5378.63`, which names a real operation, sits in a real field, parses,
  satisfies every bound, and prices a different loan. Nothing in the output is
  wrong on its own terms, so only the user's own utterance can falsify it.
- **`mortgage_verification.cppm` catches exactly that, and since `c873f9e` it IS
  wired into the serving path.** `mortgage_assistant_service.cpp` imports it and
  `ParseOperation` gates on the verdict: `Proven` is the only path to a
  `FinanceParams`, and `Unsafe` / `Indeterminate` refuse. Its five gates pass 72
  checks / 0 failures. Before that commit its only consumer was
  `test_mortgage_verification`, and `ParseOperation`'s own structural checks —
  which the test proves return Proven on `5379.00` — were all that guarded the
  RPC.

## Pro tier and quota

Entitlement flows through Supabase `auth.users.app_metadata.tier` (never
`user_metadata`, which is browser-writable) or a signed licence
(`HMAC-SHA512[0:32]`, base64url). `profiles.tier` is dead; nothing reads or
writes it.

**`PRO_GATE_MODE` is `enforce` in production, not `warn`.** This file said
`warn` — observe-only, logging would-denies without denying, "until a live
checkout round trip is proven" — and the live value was read as `enforce` on
2026-08-06. Both assistants therefore answer a non-Pro caller with
`PERMISSION_DENIED`, and an anonymous `ParseOperation` from
mortgagefvcalculator.com is refused outright. **The condition that `warn` was
waiting on has still not been met**: the live checkout round trip is unproven,
so nothing has demonstrated that a user who pays actually receives the
entitlement the gate now requires. Whether to hold `enforce` or fall back to
`warn` until that round trip passes is a business decision, not a documentation
one — this paragraph records which way the switch is actually set, not which
way it should be.

The gate is `check_assistant_entitlement` (`api_key.cpp`), shared by both
assistants on purpose: its rationale is the cost asymmetry of running a 0.6B
model at all, which is not a property of which model it is. Only the copy and
the log label are per-surface (`kStrategySurface` / `kMortgageSurface`).

`quota.cpp` collapses **every unkeyed caller into one shared `~anonymous`
bucket**. A per-user-looking anonymous limit is therefore a site-wide limit; it
is sized as what it is:

| tier | req/min | compute-units/hr | scope |
| --- | --- | --- | --- |
| anonymous | 6000 | 120,000 | shared site-wide |
| free | 120 | 3,600 | per caller |
| pro | 600 | 240,000 | per caller |
| partner | 2400 | 1,200,000 | per caller |

`limits_for_tier()` silently falls back to the *anonymous* allowance for a tier
it does not recognise while still labelling refusals with the requested tier.
The live `QUOTA_POLICY` defines `pro`; the example in `docs/FINANCE_API.md` does
not. Do not "fix" the live policy by copying the doc.

## Features & Capabilities

1. **Futures & Options Strategy Modeler:**
   - Options Strategies: Bull Call Spread, Bear Put Spread, Straddle, Strangle, Iron Condor, Call Butterfly, Covered Call.
   - Futures Strategies: Futures Outright Long/Short, Futures Calendar Spread, Inter-Commodity / Crack Spread, Covered Futures Call (FOP), Cash & Carry / Basis Trade.

2. **Interactive Symbol Selector:**
   - Live ticker search across Equities (`SPY`, `QQQ`, `NVDA`, `AAPL`, `TSLA`), Futures (`ES`, `NQ`, `CL`, `GC`, `ZB`), and Crypto (`BTC`, `ETH`).
   - Automated asset class classification (`EQUITY`, `FUTURES`, `CRYPTO`).
   - Fast-select preset pills and custom spot price simulation.

3. **Full Complement Market Chains:**
   - **Options Chain:** Dynamic ITM/ATM/OTM strikes (15+ strikes), Bids, Asks, Deltas, Volumes, Open Interests, IVs, and Weekly/Monthly/Quarterly expiration date filters.
   - **Futures Term Structure Chain:** Contract codes (`ESU26`, `ESZ26`, `ESH27`), delivery months, days to expiry, forward prices, basis vs spot, cost of carry yield (% p.a.), volume, and open interest.

## Deployment Details

- **Frontend Deployment:** `cd frontend && npm run build && npx wrangler deploy` — a
  **Workers static-assets** deployment (`frontend/wrangler.toml`, `[assets] directory = "./out"`).

  **`wrangler pages deploy` does not deploy this site.** The Pages project
  `optionsandfuturescalculator` still exists and still accepts deployments, so the
  command succeeds and prints a URL — but the live domains are **Workers custom
  domains** bound to the `optionsandfuturescalculator` Worker, so a Pages deploy
  changes nothing a user can see. This cost five "completed" frontend deploys: each
  updated Pages while the apex kept serving a months-old bundle whose Supabase URL
  was still `http://localhost:8000` with a placeholder anon key, so sign-in — and
  therefore the JWT route to Pro — was dead in production the whole time.

  The tell, if it happens again: the apex returns `cf-cache-status: HIT` with **none**
  of the Pages response headers (`access-control-allow-origin`, `referrer-policy`,
  `x-content-type-options`) that `pages.dev` returns, and a cache purge does not change
  it. The apex/`www` DNS records are `AAAA 100::` (the discard prefix Cloudflare writes
  for a Workers custom domain), not a CNAME. Confirm with
  `GET /accounts/{acct}/workers/domains` — if the hostname is listed there, Pages is
  not serving it and never was.

  Do not "fix" this by attaching the domains to the Pages project: while the Workers
  custom domain owns the hostname, the Pages attachment sits at `status=pending`
  forever and does nothing.
- **Backend Deployment:** Railway CLI (`railway up --detach` linked to project `fearless-amazement` service `options-calculator-backend`).
- **Database Schema:** Applied `backend/migrations/01_init.sql` to Railway Postgres via `psql`.

### DNS (Cloudflare zone `optionsandfuturescalculator.com`)

Recorded here because none of it is otherwise represented in the repository.

| Record | Target | Proxied |
| --- | --- | --- |
| `optionsandfuturescalculator.com` AAAA | `100::` (Workers custom domain) | yes |
| `www` AAAA | `100::` (Workers custom domain) | yes |
| `api` CNAME | `3nw3v5qd.up.railway.app` | **no** |
| `_railway-verify.api` TXT | `railway-verify=8f82c9d1...` | n/a |

Three constraints on the `api` records, each of which broke the endpoint once:

1. The CNAME must target the **per-domain** endpoint Railway mints when the
   custom domain is attached (`3nw3v5qd.up.railway.app`), *not* the service
   domain `options-calculator-backend-production.up.railway.app`. Railway's
   router rejects the hostname otherwise and answers with
   `x-railway-fallback: true` and a 404.
2. It must stay **un-proxied**. Behind Cloudflare's proxy Railway resolves the
   record to anycast addresses, sees no CNAME, and can neither verify ownership
   nor complete the ACME challenge.
3. The `_railway-verify.api` TXT record must persist. It is what moves the
   certificate out of `VALIDATING_OWNERSHIP`, and removing it breaks renewal.

Attaching a custom domain needs an account-scoped Railway credential. If
`railway domain <name>` returns `Unauthorized` while read commands succeed,
`~/.railway/config.json` holds only a read-scoped `accessToken`; run
`railway login` or use the dashboard.

## Build Commands
- **Frontend Production Build:** `cd frontend && npm run build`
- **Frontend Dev Server:** `cd frontend && npm run dev`
- **Backend Docker Build:** `docker build -t options-backend backend/`
