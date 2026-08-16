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

**The replacement hosting mechanism EXISTS and both models are deployed and
loaded.** This section said the opposite until 2026-08-12 — "no model is in the
deployed container", "each assistant answers `MODEL_UNAVAILABLE`" — and that was
stale, not merely imprecise. Verified against the running engine:

```
Loading GGUF model from /app/model/mortgage-assistant.gguf
Loading weights: layers=28, hidden=1024, vocab=151936, threads=48
Mortgage assistant ready: backend=sensen device=cpu
Mortgage assistant model is LOADED
```

Both assistants load the same way, from `/app/model/`, `backend=sensen
device=cpu`, at every boot. `MODEL_URL` / `MORTGAGE_MODEL_URL` on the Railway
service point at a private artifact host, with `MODEL_SHA256` /
`MORTGAGE_MODEL_SHA256` set alongside them.

The history, which still matters: the private HuggingFace repository the fetch
originally used was **deleted on 2026-08-05** at the owner's instruction — the
weights are proprietary trade secrets and do not belong on a third-party
registry, private or not. Do not re-upload to any model registry to "restore"
anything; the current transport replaced that deliberately.

What did NOT change, and is the part to hold on to:

- The model is fetched at IMAGE BUILD time with the checksum pinned. It cannot
  travel through `railway up`, which enforces an upload deadline that 62 MB
  already failed.
- A model is checksum-verified before anything uses it, staging is not
  verification, and the checksum that counts is round-tripped from wherever the
  bytes are actually served.
- An empty `MODEL_URL` is still a SUPPORTED build, not a broken one: that
  assistant answers `MODEL_UNAVAILABLE` and every other service is unaffected.
  One model, both, or neither is a valid image.
- `backend/Dockerfile`'s `backend/models/` build-context staging path still lets
  a LOCAL `docker build` use a locally held GGUF.

`docs/MORTGAGE_MODEL_DISTRIBUTION.md` carries the checksums. Its status banner
predates this correction — read this section first.

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

### Verified end to end against production, 2026-08-12

The whole mortgagefvcalculator.com backend path was exercised through the live
ingress. Envoy's `grpc_json_transcoder` covers all four services with
`auto_mapping: true`, so every RPC is reachable as
`POST /<package>.<Service>/<Method>` with a JSON body — which is how this was
checked without a browser, and how to check it again.

| check | result |
| --- | --- |
| `Finance/ComputePayment` (495000, 0.005625, 360) | `-3210.560578012665289866` |
| `ParseOperation`, anonymous | HTTP 403, code 7, the `kMortgageSurface` refusal |
| `ParseOperation`, partner key | HTTP 200 — the gate ADMITS |
| Both assistant models | LOADED at boot (see the model section above) |

The `ComputePayment` figure is the point of that first row: it is an exact
18-place `BigDecimal`, it matches the closed-form annuity payment computed
independently, and it matches the P&I the live site displays for its default
scenario. Three independent derivations of one number.

**The sign is NEGATIVE and the row is meant to be compared literally.** This
line recorded the magnitude until 2026-08-13, which reads as a mismatch the next
time anyone diffs it against a live call. `-3210.560578012665289866` is the
standard time-value-of-money convention — a payment against a positive present
value is a cash OUTFLOW, exactly as Excel's `PMT` returns it — and the site
displays the magnitude because a P&I figure is quoted unsigned. Re-checked
2026-08-13: the digits are identical to the recorded value and
`finance_service.cpp` has not changed since 2026-08-10, so the sign was dropped
in transcription rather than flipped in code.

**The admit direction was tested with the issued partner key**
(`config/keys/`, gitignored, `tier partner`, `scopes finance, assistant`,
origin-locked). Testing only the refuse direction proves nothing — a gate that
refuses everyone passes it.

**The verification layer earned its place on the very first admitted call.** The
model answered a 495000 utterance with `present_value = 304000.00`, and
`mortgage_verification.cppm` refused it:

> `"present_value" = 304000.00 does not correspond to anything in the request
> (the nearest figure you gave is 495000)`

That is the documented dangerous failure — a corrupted value that parses,
satisfies every bound, names a real field, and prices a different loan —
caught in production rather than in a test.

Two of three canonical utterances then parsed correctly:
`ComputePayment{rate 0.005000, periods 360, present_value 300000.00}` and
`ComputeAmortization{annual_rate 0.0450, term_months 180, loan_amount
200000.00}`. That is consistent with the documented 27.8% params exact-match:
expect refusals, and expect them to be honest.

### A false refusal, found in production and fixed

"Compute the future value of 1000 at 5% for 10 years" was refused with a message
that contradicts itself:

> `"periods" = 10 does not correspond to anything in the request (the nearest
> figure you gave is 10)`

`finance.proto` documents `rate` as PER-PERIOD and `periods` as a count of those
same periods — the pair is only meaningful together. The grounding gate checked
them **independently**: the rate grounded against any cadence in M3
`{1,2,4,12,26,52}`, while `periods` grounded against months by convention (a
hardcoded x12, with the identity candidate suppressed for a Years-tagged
literal). Right for a mortgage, wrong for the rest of the TVM surface, which
shares that same generic pair — so an annual rate with ten annual periods had no
admissible interpretation and a correct parse was refused.

**The naive fix is wrong and was not taken.** Admitting the identity candidate
would equally admit `periods = 30` against a MONTHLY rate — a thirty-month loan
answered as thirty years, which is the slip the suppression exists to catch.
Instead the cadence is INFERRED from the emitted rate
(`infer_periods_per_year`), and `periods` is grounded against that: x12 when the
rate was divided by 12, x1 when it is annual. One rule, both cases.

It **tightens** the gate as well as loosening it. An annual rate paired with
`periods = 360` used to be `Proven` — months grounded by convention while
nothing checked the rate against them — and is now refused. That hole was open
the whole time and nothing covered it.

`payment = 0` also joined the convention values, mirroring `future_value = 0`.
Without it a LUMP-SUM present/future-value question is refused on `payment` even
once `periods` grounds correctly.

All four combinations are asserted, because the fix had to loosen one direction
without loosening the other: annual/annual passes, monthly/monthly passes,
monthly-rate-with-`periods=30` refused, annual-rate-with-`periods=360` refused.
Mutation-checked — restoring the hardcoded x12 reproduces the production message
verbatim AND flips the mismatched pair back to `Proven`.

## Pro tier and quota

Entitlement flows through Supabase `auth.users.app_metadata.tier` (never
`user_metadata`, which is browser-writable) or a signed licence
(`HMAC-SHA512[0:32]`, base64url). `profiles.tier` is dead; nothing reads or
writes it.

**`PRO_GATE_MODE` is `enforce`, by decision, in Railway and in `config/.env`.**
An earlier version of this file said `warn` — observe-only, logging would-denies
without denying, "until a live checkout round trip is proven." That was stale in
both halves: the live value was already `enforce` when it was read on
2026-08-06, and the owner confirmed `enforce` as the intent the same day. It is
not waiting on anything, and changing it is not a documentation edit.

Two gates, both live, **both verified against production in both directions**:

| gate | free | Pro |
| --- | --- | --- |
| `check_strategy_entitlement` (`CalculateStrategy`) | 1 leg | 2+ legs |
| `check_assistant_entitlement` (both assistants) | — | every call |

Anonymous 2-leg → `PERMISSION_DENIED`. The same request with a signed licence →
`maxProfit 1275, maxLoss -725, breakEven 587.25` on a 580/600 bull call spread,
which is the closed-form answer for a 7.25 debit. **Verify the ADMIT direction,
not only the refuse direction** — a gate that refuses everyone passes a
refuse-only test, and this one stands in front of the product's entire reason to
exist.

`check_assistant_entitlement` is shared by both assistants on purpose: its
rationale is the cost asymmetry of running a 0.6B model at all, which is not a
property of which model it is. Only the copy and the log label are per-surface
(`kStrategySurface` / `kMortgageSurface`) — see that struct's comment for why
the message is stored whole rather than templated.

What enforce means at the two client surfaces, neither of which is a backend
concern and both of which are now user-visible:

- optionsandfuturescalculator.com is a multi-leg calculator, so an anonymous
  visitor is refused on every strategy except a lone call or put. That refusal
  renders as `UpgradePrompt` — heading **"Needs Pro"**, the engine's own
  sentence, and the checkout buttons — rather than through the error branch
  under "Unavailable" in loss red, which is what it did until 2026-08-06.
  `useCalculatorStore` discriminates on the gRPC **status code** (7), never on
  the message text, which was reworded twice in a single day.
- mortgagefvcalculator.com gets `PERMISSION_DENIED` on every anonymous
  `ParseOperation`, so that integration needs a Pro credential before the
  assistant does anything at all. The `sensen.finance.Finance` RPCs it would
  otherwise call are ungated — which is exactly what `kMortgageSurface`'s
  refusal points the caller at.

Task #44 — a live $0 Stripe round trip — remains unproven and needs owner
authorization, because it puts a real card on an account shared with other apps.
Enforce is live independently of it.

`quota.cpp` collapses **every unkeyed caller into one shared `~anonymous`
bucket**. A per-user-looking anonymous limit is therefore a site-wide limit; it
is sized as what it is:

| tier | req/min | compute-units/hr | scope |
| --- | --- | --- | --- |
| anonymous | 6000 | 120,000 | shared, but **per replica** — see below |
| free | 120 | 3,600 | per caller, **per replica** |
| pro | 600 | 240,000 | per caller, **per replica** |
| partner | 2400 | 1,200,000 | per caller, **per replica** |

**Every number in that table is multiplied by the replica count, and
`railway.json` sets `numReplicas: 3`** (raised from 2 on 2026-08-10). `callers_` is an in-process
`std::unordered_map` behind a process mutex (`quota.cpp:82`), a Meyers singleton
with no persistence of any kind, so each replica refills its own buckets. The
effective anonymous ceiling is **18,000 req/min at three replicas**, not 6000,
and which replica a caller lands on decides whether they are refused. Every
other row multiplies the same way: free 360, pro 1800, partner 7200. An earlier version of this
file called the anonymous bucket "shared site-wide" full stop; that was written
before `numReplicas: 2` arrived in `9445ac5` and was wrong from that commit
onward. Envoy's local rate limit (100 tokens, 10/s refill, `envoy.yaml:105-127`)
is per-replica for the same reason — it is a per-container sidecar, not a
cross-replica balancer.

**`numReplicas: 3` is intentional** (raised from 2 on 2026-08-10; 2 was
confirmed intentional on 2026-08-08 for the reasons below, and all of them still
apply). The reason is crash and host-failure availability for two live sites
plus capacity headroom — **not** deploys. Railway's blue/green deploy overlap is gated on `/healthz` at the
*deployment* level and gives zero-downtime releases identically at one replica,
so "we need 2 for deploys" is a wrong argument that has already been made once
in this repository. What 2 replicas genuinely buys: `start.sh:185-189` exits the
container when either the engine or Envoy dies, so at one replica every crash is
a full outage for both sites lasting restart plus the 20 s
`healthcheckInitialDelay`; and `restartPolicyMaxRetries: 3` means a
crash-looping deployment stops being restarted at all, which is survivable at
two replicas and a hard outage at one.

**Why three rather than two, as of 2026-08-10 — and what it does NOT buy.**
Three is for plain availability and capacity headroom: one more simultaneous
container loss survivable, and one more instance absorbing the crash-restart
window that `restartPolicyMaxRetries: 3` can otherwise turn into a hard outage.

It buys **nothing toward running a consensus cluster**, and the first version of
this note claimed otherwise. Raft across Railway *replicas of one service* is
structurally impossible here, for reasons that have nothing to do with how many
there are:

- **No stable per-replica address.** `<service>.railway.internal` resolves to a
  randomly chosen replica IPv6, not to a node. `RAILWAY_REPLICA_ID` is a UUID in
  each container's own environment, for logging — peers cannot query it, and
  there is no ordinal index (the feature request for one is open, uncommitted).
  A `dig` against the internal name does return one record per replica, so the
  current address SET is discoverable, but nothing binds an address to a durable
  identity across a restart.
- **Volumes cannot be attached to a service with `numReplicas > 1` at all.**
  Railway disallows the combination. So there is nowhere for a node to persist
  its Raft log, currentTerm or votedFor — the exact state Raft must fsync before
  replying to any RPC. This is also why the Postgres substrate is not merely the
  better choice for durable work but the ONLY one available while replicas are
  in use.
- **Every deploy is a whole-cluster replacement.** Railway's zero-downtime
  release is a blue/green cutover at the *deployment* level: an entirely new set
  of containers is stood up, health-gated, then traffic cuts over and the old set
  disappears together. Raft's reconfiguration protocol is designed for
  incremental single-node membership change, not for its whole quorum being
  swapped at once.

Railway's own idiom for N individually-addressable, durable peers is **N
separate services**, each with its own volume and its own stable
`*.railway.internal` name — which is how their MongoDB replica-set template is
built. That is a materially different topology from a `numReplicas` bump, and it
is not what this service is.

The known costs, all accepted, and now scaling by three rather than two: the
quota multiplication above, ~3x model memory once weights are deployed,
per-replica assistant admission queues (one replica can answer
`RESOURCE_EXHAUSTED` while another's owner thread idles), and ~3x Alpaca calls
with independently-tripping circuit breakers. Container cost rises by half
again over two replicas. None is a correctness
break — every RPC is stateless per request and there is no durable local state
to split-brain, because there is no volume. The admission-queue fragmentation is
what the shared inference queue is being built to fix.

## SGEE queue cluster (`sgee-queue-1/2/3`)

The N-separate-services topology described just above is no longer hypothetical.
Three services in the same project and environment — `sgee-queue-1`,
`sgee-queue-2`, `sgee-queue-3` — each run **one** replica of
`backend/Dockerfile.queue-node` with **its own `/data` volume**, and together
they are one Raft cluster. Do not "simplify" this into one service with
`numReplicas: 3`: Railway forbids volumes with replicas, and replicas share a
hostname, so the peers could not address each other.

**It is a non-authoritative mirror. Postgres remains the system of record**, and
`docs/SGEE_QUEUE_CLUSTER.md` carries the evidence for why.

**The first deployment crash-looped, and how it presented is the lesson.** All
three nodes died on an uncaught `std::bad_variant_access` seconds after boot,
while **Railway reported every deployment SUCCESS** — the healthcheck passes
before the throw, so `railway status` is not evidence a service is running; read
the logs for repeated boot banners. It was invisible for a whole deployment
cycle because the node never flushed stdout: in a container that is a pipe and
therefore fully buffered, so eight boot lines never reached the log stream at
all. The cause was a data race on `ReplicatedQueueRuntime::inbound_` — the gRPC
transport delivers frames on its **own** drain thread, while the runtime's
header argued no lock was needed. That argument is about *reentrancy* and is
correct; it says nothing about *concurrency*, and it held only while the sole
transport was the in-process one. Fixed, but **not confirmed fixed in place** —
it never reproduced locally, and the local three-node durability test passes
with the race present.

**The cluster was then down for a day on a different defect (2026-08-12), and
that one is the sharper lesson.** `DurableAppender::create` treated a failed
`io_uring_queue_init` as fatal, turning a COMPILE-time capability into a
RUN-time requirement — and container runtimes block `io_uring_setup` by seccomp.
`TaskBroker::open` ends by sweeping leases that expired while the process was
dead, and that sweep is the **first** caller of `begin_batch`, so a node that
died holding a lease could never reopen its own WAL. It ran for weeks because
`sweep_expired` returns early when nothing has expired.

Three things about it are worth carrying to any similar diagnosis:

- **It presented as a corrupt WAL and was nothing of the kind.** The log read
  cleanly every time; only the WRITE failed. Wiping the volumes would have
  "fixed" it, destroyed the only evidence, and left a defect that would re-brick
  the rebuilt cluster the first time a node died mid-lease.
- **All three failed identically and simultaneously**, which rules out
  independent disk corruption and points at replicated state — the expired-lease
  set is the same on every node.
- **Four layers each widened the error until it said nothing:**
  `WalError` (six values) → `QueueError::WalError` → `ConsensusError::QueueError`
  → "failed to create driver". Every one of those types already had a
  `to_string`. A day of downtime produced the single word `QueueError`.

Fixed by making the ring a runtime optimization (`create()` leaves the flag
false, `commit()` dispatches on it), and gated by `QueueNodeBlockingIoTest`,
which forces the fallback with `SGEE_FORCE_BLOCKING_IO=1` — no other test could
reach that path, because the async backend works on every developer and CI host.
The Win32 IoRing and IOCP branches carried the identical defect and are fixed
the same way. See `docs/SGEE_QUEUE_CLUSTER.md`.

**The promotion audit ran on 2026-08-12. The precondition it was waiting on now
passes, and it found a different blocker.** The tuned Raft timing cured the
churn — term 2981 held across the sampling window, `last_applied` in lockstep,
the leader's `sweep_successes` climbing. But two of three nodes carried
`tick_errors` (108 and 121) that turned out to be *divergence detectors firing*,
and `apply_committed` was consuming the whole committed batch before applying any
of it — so a failure abandoned every entry behind it, permanently, while
`last_applied` still reported the node caught up. That number is Raft's cursor,
not a count of anything the broker applied, so **three nodes agreeing on it is
not evidence their state machines agree.** Fixed in SGEE `fbcd2d63` with a
peek/mark pair, plus `commit_index` published beside `last_applied` on `/statusz`
because the gap is the only way to tell caught-up from stalled. Promotion needs
the fix deployed and the audit re-run. See `docs/SGEE_QUEUE_CLUSTER.md`.

**`INFERENCE_QUEUE=sgee` is LIVE on the engine as of 2026-08-13** — promoted,
rolled back to `postgres` the same day on three defects, and re-promoted once
all three were fixed, deployed and verified **under concurrent load**. Postgres
remains the system of record and the degrade target.

**The rollback is the part worth remembering.** The original flip was verified
with ONE request, and a single request cannot put two leases in flight — it was
structurally incapable of reaching the defect that broke this, so reading its
logs more carefully would never have helped. Six real requests found the first
defect in minutes; the re-promotion was gated on 24 concurrent ones. When a
check passes, ask what its shape excluded.

The promotion itself was carried out in five staged deploys
(`docs/superpowers/plans/2026-08-12-sgee-queue-promotion.md`). It needed five
because the queue had no way to return an ANSWER: `CompleteRequest` carried no
result, `Task` had no result field, and there was no RPC to read a task back at
all. A submitter on one replica could not learn the outcome of a job executed on
another, which is the whole reason `PostgresAdmission` exists — so this was a
protocol change, never a configuration flip.

Verified at the flip: 6 `model is LOADED` (3 replicas × 2 assistants), 3
mortgage + 3 strategy logging `INFERENCE_QUEUE=sgee`, all three nodes agreeing
`last_applied == commit_index`, `tick_errors: 0`. A real `ParseOperation`
through the live ingress with the partner key answered in **2.18 s** with
**zero** degrade-path warnings — and that last clause is the whole proof, because
a degraded request returns the same answer. Every fallback branch in
`SgeeAdmission` logs; silence is what distinguishes "served by the cluster" from
"served locally after the cluster failed".

**That verification was still not enough, and the gap was in what it measured.**
It was ONE request. A single call cannot put two leases in flight, so it could
not reach the concurrency defect below no matter how carefully it was read. Six
real requests found it in minutes.

**Re-promotion gate, 2026-08-13 — use this shape, not the single call.** 24
concurrent `ParseOperation`s through the live ingress (8 then 16), all HTTP 200,
against: `last_applied == commit_index` on all three, `tick_errors: 0`,
`apply_rejections` **equal across replicas and unchanged**, term stable (no
election churn), and **0 `[WARN]` / 0 `[ERROR]` in the engine**. That last one is
the negative proof, and it is checked on the right predicate: every degrade
branch in `SgeeAdmission` calls `logger::...warn()`, so zero warns is the
statement. Do not grep for hand-picked phrases — an earlier pass matched nine
lines that were all benign, six being the boot INFO announcing that a fallback
*exists* and three being Envoy's header-map dump containing `x-envoy-degraded`
as a header NAME.

**Defect 1 — propose-vs-apply skew on `lease()`.** `earliest_leasable()` reads
APPLIED state, so two concurrent `lease()` calls on the leader picked the SAME
task and proposed it twice. Followers logged `tick_errors` 5 and `dlq_depth` 2
while the leader showed 0/0 — divergence, presenting as `broker_error=Corrupt`.
`enqueue()` twenty lines above had always compensated for exactly this with a
proposed-but-unapplied counter; `lease()` never did. Fixed with
`proposed_leases_unapplied_`.

**Defect 2 — mutate-then-validate bricked all three nodes.** `apply_one` called
the *choosing* `broker_.lease()` and checked the resulting id against the
command's afterwards, so every retry of an unapplicable entry consumed one more
pending task until the queue was empty forever. Fixed with
`TaskBroker::lease_specific`, which leases the task it was NAMED and is
idempotent on an already-leased one.

**Defect 3 — a rejection is an answer, and retrying it froze the cluster.** With
defect 2 fixed, all three nodes were STILL stuck at `last_applied` 29285 with
`commit_index` past 29700 and `tick_errors` in the tens of thousands. `fbcd2d63`
had correctly stopped a failed apply from consuming the batch behind it — and
thereby made *every* apply failure stall, including ones that can never resolve.
A `BrokerLease` naming a task the local broker cannot lease (swept to the DLQ,
already terminal, never seen) was retried forever.

The distinction the fix rests on: **a committed entry is a FACT every replica
must CONSUME at its index, but whether it has an EFFECT is the state machine's
business. The transition must be TOTAL — a rejection is an answer, not a refusal
to answer.** `is_deterministic_rejection` splits them: `UnknownTask`,
`NotLeased`, `QueueEmpty`, `StaleFencingToken`, `PayloadTooLarge` and
`InvalidArgument` are computed from replicated state, so every replica reaches
the same answer from the same bytes and recording "no effect" converges exactly
as recording an effect does. `WalError` and a decode failure are statements
about THIS replica's local storage, say nothing about the command, and still
stall — writing a local disk fault into replicated history would be the worse
error. Both directions are gated
(`ReplicatedBroker_DeterministicRejection_IsConsumedNotRetriedForever` and the
existing `0xEE` decode test), and the first is mutation-checked: making
`UnknownTask` retryable reproduces the production stall.

**Defect 4 — the ROOT CAUSE of the divergence: the lease deadline was computed
from each replica's own clock.** `TaskBroker::lease_specific` set
`deadline = cfg_.clock_ms() + window` at APPLY time, and `BrokerLease` carried
the window but **not the instant it is measured from**. `BrokerSweep` carries the
LEADER's `now_ms` and is compared against those per-replica deadlines, so a sweep
landing between them expired a task on the leader that was still Leased on the
follower.

**The driver is replication delay, not clock skew.** A follower applies a
committed entry tens of milliseconds after the leader, so its deadline is later
by exactly that delay with perfectly synchronised clocks. NTP cannot help; the
fault is computing anything replicated from a local clock at apply time. This is
the SAME defect as `BrokerEnqueue::enqueue_ms`, fixed the same day, twenty lines
away in the same file — one instance was found and its sibling was not.

Shipped readers-first in two stages, because `BrokerLease`'s body is exactly 24
bytes and the per-tag size check IS the versioning: Stage 1 accepts 24 and 32 and
changes no bytes on the wire (the encoder emits 24 while the field is zero,
mirroring `BrokerComplete`'s empty-result rule); Stage 2 is the one-line writer
flip. Both shapes are fixed width, so there is no dead zone.

**The existing test harness could not have caught it, and that is the lesson.**
`Cluster` gives every node ONE virtual clock, so anything computed from
`cfg_.clock_ms()` at apply time is identical on every replica in the harness and
different on every replica in production — a test written there passes whether or
not the bug exists. The fix required teaching the harness per-node clock SKEW
before the property could even be stated.
`ReplicatedBroker_VisibilityDeadlineIsTheLeadersOnEveryReplica` is
mutation-checked, and two traps live inside it: zero is the "no timestamp
carried" sentinel, so a skewed clock clamping to zero reproduces the bug's
signature from the harness rather than the code; and the mutation check first
"passed" against a **stale binary**, because `ninja`'s output was piped to
`grep -c` and `ctest` ran before the relink finished.

**What it was costing:** node 1 was the last replica never rebuilt and it was the
leader, so every engine writeback failed and **every inference request fell back
to local execution** while returning correct answers and HTTP 200. 12 writeback
warnings before the rebuild, 0 after; 16 concurrent requests went 10.09 s ->
8.65 s. Its snapshot was 355,772 bytes against the rebuilt nodes' 26–48 KB.

**The only exit from that stalled state is wiping the volumes**, which destroys
the evidence and re-bricks the rebuilt cluster the next time it happens — the
same trap as the io_uring outage. `/statusz` now publishes `apply_rejections`
beside `commit_index`; **compare it ACROSS replicas, not against zero.** Equal
totals are the healthy state and unequal totals mean the state machines
diverged, which agreement on `last_applied` cannot rule out — that number is
Raft's cursor, not a count of anything the broker did.

**Defect 5 — the id skew's ROOT CAUSE: the leader predicted a TaskId from a
local counter instead of from the log.** `enqueue()` computed
`broker_.next_task_id() + proposed_enqueues_unapplied_`. The first term reflects
only **applied** state; the second counted only proposals **this** leader had
made since the last term change. An enqueue entry that is already in the log,
will certainly apply, and was proposed by somebody else is in neither — which is
precisely what a new leader inherits, and nothing gates `enqueue()` on
`last_applied == commit_index`.

Found by reading production, not code. Node 1's log shows nine elections with
`last_applied` frozen at 34298, then:

```
DIVERGENCE: applied BrokerEnqueue as id 538 but the leader logged id 426 (total 1)
```

**+112 on the FIRST mismatch and on all 128 that followed** — 112 being the size
of the leader's unapplied enqueue backlog at the moment it predicted. Both
counters then advance in step, so the offset never closes. At an identical
`last_applied` of 36753 the three nodes' live sets were 0 / 16 / 93.

**The comment on that detector blamed the wrong thing, and the detector was
still right.** It cited a rolling deploy of mismatched payload bounds — a real
path, since fixed — but that cannot be this: `TaskBroker::enqueue`'s only
pre-reserve bound is `payload.size() > kMaxUserBytes`, a constant applied to
bytes carried in the command, so every replica computes it identically. When an
instrument fires, its stated cause is a hypothesis, not a finding.

`pending_enqueues_in_log()` replaces the counter, reading **one byte** per tail
entry (the command tag is the first byte of the body) over the
`last_applied..last_log_index` window. It also removes the term-change reset:
a truncated proposal leaves the log, so a **derived** quantity is right by
construction where a **cached** one had to be invalidated by hand — and that
hand-invalidation was wrong in both directions, forgetting committed proposals
that would certainly apply while `apply_one` decremented for enqueues this
leader never proposed. Same lesson as defect 4: a replicated decision must be
derived from replicated state, not from a local counter shadowing it.

It stops NEW skew and reconciles nothing. An already-diverged replica still
needs its volume wiped and rebuilt.

**Two digests now answer that properly, and they answer DIFFERENT questions.**
`apply_digest` folds `(index, command tag, outcome, assigned id)` into a
running splitmix64 hash — O(1) per entry, so it can run on every apply. It is
**only comparable between nodes at equal `last_applied`**, and it resets at
every boot, so it compares a node against its peers within one process lifetime,
never across a restart. `state_digest` hashes replicated task CONTENT in sorted
id order — O(tasks), refreshed on the `stats_interval` cadence — and detects
divergence that has ALREADY happened, which is the question `apply_digest`
cannot answer because a node that booted at a different index folded a different
range.

**Both are published as hex STRINGS, deliberately.** A 64-bit digest exceeds
JavaScript's exact integer range, so a JSON number would be rounded by `jq` and
by every browser — two different digests would compare EQUAL. A divergence probe
that reports agreement because its transport lost precision is worse than no
probe.

**`state_digest` excludes every per-replica field** — `fencing_token` (a locally
minted WAL LSN), `visibility_deadline_ms` (a local clock), `lease_owner` — **and
every TERMINAL task**. This is load-bearing rather than fastidious: a digest that
included them would differ on healthy nodes, fire constantly, be disbelieved, and
get switched off.

**The terminal-task exclusion was learned the hard way, and it is the most
important line here.** `state_digest` originally hashed every task in the index.
Completed and Dead tasks are reclaimed by `evict()`, which keeps a task only
while `now_ms - terminal_ms < retention_ms` — and **compaction is LOCAL, running
on every node regardless of leadership**
(`replicated_queue_runtime_driver.cppm`). Each replica therefore drops terminal
tasks against its OWN clock at its OWN compaction moment, and two perfectly
healthy replicas hold different terminal sets by design. The digest was measuring
local retention timing and calling it divergence.

**So `dlq_depth` is NOT a divergence signal either, and never was.** It counts a
locally-evicted set. The 3 / 0 / 3 and 3 / 0 / 20 readings say nothing about
whether the state machines agree.

Their first live use, on 2026-08-13, is what exposed this — by producing a result
that could not be true. Three nodes disagreed; a follower **rebuilt from scratch
out of the leader's own snapshot still disagreed at an identical
`last_applied`**, which is impossible for genuine divergence. The instrument was
wrong, not the cluster. `state_digest` now hashes only the LIVE set (Pending +
Leased), gated by `StateDigest_IsInsensitiveToLocalRetention` and
mutation-checked against the production symptom.

Known limit, accepted: a divergent RESULT on an already-completed task is
invisible to it. It has to be — a completed task may be evicted on one replica
and not the other, so no hash over completed tasks is comparable across replicas
at all, and an incomparable number is worse than a missing one.

What survived as a real signal is `apply_rejections` on the followers
(`BrokerLease:NotLeased`), which is about LIVE tasks and cannot be explained by
retention. `docs/SGEE_QUEUE_CLUSTER.md` carries the full account, the preserved
snapshots and the rebuild procedure.

**Stage 1 (readers) and Stage 2 (writers) are a two-phase deploy and the order
is not negotiable.** The `BrokerComplete` frame carries no version byte and no
length prefix — it is `[tag u8][fields]`, so the per-tag exact-size check IS the
versioning — and since `fbcd2d63` a frame a node cannot decode is no longer
silently dropped: `apply_committed` stops without marking it, `last_applied`
freezes, and the node retries the same entry forever. Writers before readers is
therefore **a stalled node per un-upgraded replica**, not a degraded window.
Stage 1 shipped readers for all three formats (index snapshot, WAL
`TaskCompleted`, replicated `BrokerComplete`) and wrote nothing new.

Two shapes are told apart by SIZE alone, which forces a **refused dead zone** —
9..11 bytes for the WAL record, 17..19 for the command body. Reading one as a
short v2 would build a length out of whatever bytes happened to follow, which is
indistinguishable from a real one. An empty result is deliberately encoded as
the v1 shape, so an empty result and a v1 record are the same bytes: the same
fact, written the same way, still readable by a binary that predates v2. The
consequence worth knowing is that the Stage-2 writer flip is **inert** on a
cluster that is not yet recording results.

**The queue replicates results, not error text, and that is a decision.** A
failure is signalled by the task reaching terminal state `Dead`. Carrying an
error string would mean widening `BrokerFail` — a fixed 16-byte command with no
second shape any deployed node can read — plus two more WAL payloads and another
snapshot version, a second full readers-first cycle for a diagnostic the worker
already logs. A proto field the replicated log silently dropped would read as an
answer.

`INFERENCE_QUEUE=sgee` selects `SgeeAdmission`/`SgeeLeaseSource`, mirroring the
Postgres pair with the same degrade-never-hang contract. It POLLS `GetTask`
because SGEE has no await — the same shape the Postgres path already degrades to
whenever its `pg_notify` hint does not arrive, so the two share failure modes
rather than adding a set. **Postgres remains the system of record and the
fallback target** for a full deploy cycle after the flip.

**`SGEE_PEERS` on the ENGINE means the client queue port, 50053.** The nodes'
variable of the same name means consensus, 50052. Copying one onto the other
yields a process that connects to a real port, speaks the wrong protocol at it,
and fails like a network problem. Both ports carry the same mTLS credentials —
"both ports or neither", since protecting consensus while leaving the port that
ACCEPTS WORK open would secure the vote and not the queue — so the engine needs
`SGEE_TLS_CA_CERT_B64` / `SGEE_TLS_CERT_B64` / `SGEE_TLS_KEY_B64`, which are
all-or-nothing and log loudly rather than downgrading to plaintext.

Four things are easy to get wrong here:

- **`SGEE_PEERS` means two different things.** The nodes read it and dial
  **consensus, port 50052**; `backend/src/modules/sgee_queue_client.cpp` reads
  the same variable name and dials the **client queue, port 50053**. Copying one
  service's value onto the other yields a process that connects to a real port,
  speaks the wrong protocol at it, and fails like a network problem.
- **`SGEE_NODE_ID` and `SGEE_PEERS` have no image default, deliberately.** Three
  nodes that all default to id 1 form three single-node clusters that each
  accept writes, and nothing in the logs says so.
- **The deploy stages its own upload** (`deploy/queue-node/deploy.sh`). Railway
  reads `railway.json` from the root of the upload, and the one at the repo root
  describes the *engine* — `backend/Dockerfile`, `numReplicas: 3`. Railway's
  per-service escape hatches (`railwayConfigFile`, `rootDirectory`) are not
  reachable from the CLI. The stage also mirrors `.dockerignore`'s `backend/`
  exclusions to stay under the upload deadline, and retries: `railway up` timed
  out every time at 144 MB and still roughly one attempt in three at 59 MB.

  **Its failure diagnostic lied twice on 2026-08-12 and now discriminates.** It
  printed `railway logs --service`, which replays a dead session's scrollback —
  the SSL handshake errors it showed were timestamped three hours *before* the
  deployment it was blaming — and it called every failure a healthcheck failure,
  pointing a reader at a container that had never started. The discriminator is
  the newest deployment's **`imageDigest`**: absent means no image was produced,
  so no container ran and it is a BUILD failure (`railway logs --build <id>`);
  present means the container started and did not stay up
  (`railway logs --deployment <id>`). Nothing else available there tells the two
  apart, and guessing sent a whole diagnosis in the wrong direction.
- **The queue image has its OWN toolchain trip-wires, and neither names its
  cause.** Both were latent from the `import std;` conversion and both surfaced
  on the first Stage-1 deploy:
  - `Dockerfile.queue-node` must name clang by its **versioned** path.
    sensen resolves the libc++ std module as
    `dirname(dirname(CMAKE_CXX_COMPILER))/share/libc++/v1/std.cppm`, so
    `/usr/bin/clang++` — a symlink to exactly the right compiler — resolves it to
    a `/usr/share` path that does not exist, `std_module_precompile` is never
    declared, and every `import std;` in the tree dies with "module 'std' not
    found". `backend/Dockerfile` was fixed for this in `bd11d00`; the queue image
    was missed and cost a deploy.
  - It must also pass **`-lc++abi` explicitly**, which is NOT redundant with
    `-stdlib=libc++`. A from-source libc++ installs `libc++.so` as a linker
    script — `INPUT(libc++.so.1 -lc++abi -lunwind)` — so the ABI library arrives
    unasked; apt.llvm.org ships a plain symlink and nothing pulls it in. It stays
    invisible until an object references a C++ ABI symbol directly (sgee's
    `grpc_transport` does, via `__cxxabiv1::__vmi_class_type_info`'s vtable), and
    then `ld` reports the failure against **`libstdc++.so.6`** — the only C++
    runtime that *was* reachable — which reads as a standard-library mix-up and
    is not one.
- **`/healthz` is LIVENESS ONLY, and must stay that way.** `is_healthy()`
  checks running + ticked + ticked recently. It says nothing about leadership,
  and `ReplicatedQueueRuntimeDriver`'s comment claimed for months that it
  required "EITHER leading OR a leader_hint" — it never did, and implementing
  what that comment described would be a self-inflicted outage. Railway restarts
  a container and gates a deploy cutover on `/healthz`, and its zero-downtime
  release replaces the **whole set** of containers at once, so every node in the
  new set starts with no leader until they find each other and elect. A health
  check requiring a leader would 503 from all three during exactly that window:
  the cutover would never complete, the nodes would be killed and restarted, and
  the cluster could never form quorum. "Is there a leader?" is answered by
  `is_leader` / `leader_hint` on `/statusz`, where a 503 does not kill the node.
- **`SGEE_QUEUE_TOKEN` authenticates CONSENSUS, not the queue port.**
  `TaskQueueService`'s constructor takes no token at all, so anything that can
  reach 50053 can enqueue. On Railway that port is inside the project's private
  network, which is a boundary, not an authentication — **do not attach a public
  domain to it.** Until it was fixed, the token was parsed and applied to
  nothing; `tests/integration/queue_node_auth_test.sh` is what stops that
  regressing, and it discriminates (mismatched tokens must fail to elect).

`limits_for_tier()` falls back to the *anonymous* allowance for a tier it does
not recognise. The live `QUOTA_POLICY` defines `pro`; the example in
`docs/FINANCE_API.md` does not. **Do not "fix" the live policy by copying the
doc.**

The fallback direction is deliberate — an entitlement naming a tier that was
renamed must not become unlimited access — but it used to be **silent**, and the
refusal still carried the *requested* tier name. "quota exceeded for tier `pro`"
against the anonymous allowance reads as pro's own limit being hit, and sends an
operator to raise a number that is not the one in force. Two changes, since
2026-08-12:

- the refusal is labelled `pro (undefined in QUOTA_POLICY; anonymous limits)`,
  following the marker convention `(per-key)` already used for the same reason —
  a label names where the NUMBER came from, not what the caller asked for;
- the first occurrence of each unknown tier logs an error naming it. Once per
  distinct name, not per request: the condition is a misconfiguration that
  persists, so the hundredth line says nothing the first did not.

`load_policy` already rejects a `QUOTA_API_KEYS` entry naming an unknown tier,
so `admit` cannot reach this state. **`admit_identity` can**, and that is the
whole point: it takes the tier from a *verified* identity — Supabase
`app_metadata.tier` or a signed licence — and neither is checked against
`QUOTA_POLICY`, because they are issued somewhere else entirely.

`tests/test_quota_tier_label.cpp` (`QuotaTierLabelTest`) gates it, and asserts
**where each caller is cut off**, not just the string: `pro` gets 5 req/min and
`anonymous` 2, so an undefined tier being refused on its *third* request is what
proves the anonymous number is the one actually applied. Mutation-checked both
ways — dropping the marker fails one check, applying it unconditionally fails
four.

## Option chain cache

`market_data.cppm` caches chains and quotes in a `TtlCache`. The chain TTL is
**900 s (15 min)** and the quote TTL **60 s**, both overridable by
`OPTION_CHAIN_TTL_SECONDS` / `OPTION_QUOTE_TTL_SECONDS`. On an upstream failure
a stale chain is served up to `OPTION_CHAIN_MAX_STALE_SECONDS` (default 3600),
after which the request is refused rather than answered with something older.

**The cache is PER REPLICA, and `numReplicas: 3`.** `TtlCache` is an in-process
`std::unordered_map`, exactly like `quota.cpp`'s `callers_` — so there are three
independent caches, up to three upstream fetches per TTL window instead of one,
and which `fetched_at` a caller sees depends on which replica they land on.
Measured on production 2026-08-12: two page loads 17 s apart returned the same
print (warm replica), a third 52 s later returned a NEW print while no TTL had
expired and no override was set (cold replica), and a fourth reused that second
print. The win is still real — roughly a 60x cut in Alpaca calls against the
old 15 s TTL — it is just 3x smaller than the naive reading.

**Every chain carries `fetched_at` (RFC3339, `ChainResponse` field 8), and the
UI's LIVE badge is derived from it, not from status.** Under a 15-minute TTL a
status-driven badge would pulse LIVE over quarter-hour-old quotes, which is
fabrication by labeling. `frontend/src/lib/chainFreshness.ts` owns the rule:
LIVE under 60 s, DELAYED with an as-of time otherwise. Two failure modes it
handles deliberately, because both render stale data as fresh:

- **Age is computed against the VIEWER's clock**, so a browser running behind
  the server yields a negative age that a naive `age < 60` reads as very fresh.
  Beyond a 5 s skew tolerance the age is treated as unknowable and the chip
  falls back to DELAYED, keeping the server's timestamp but dropping the claim.
- **Age computed once at render freezes at render**, so the component ticks a
  clock every 15 s; otherwise a chain fetched fresh would still say LIVE fifteen
  minutes later with nothing else on the page changing.

A missing `fetched_at` renders DELAYED, never LIVE — which is what the site did
between the frontend and backend deploys, and is the correct direction to fail.

## Advertising and the publisher-content policy

AdSense flagged this site on **2026-08-16** under *"Google-served ads on screens
without publisher-content"* — the policy covering screens with no content or low
value content. The diagnosis was a measurement, not a reading of the policy:

```
/calculator/long-call    → 753 words
/calculator/iron-condor  → 753 words   ← identical
```

All 26 `/calculator/<slug>` pages rendered the same `StrategyWorkspace` and
differed **only by the strategy name** in the `<h1>` and `<title>`. Neither they
nor the home page contained one sentence of prose — `grep '<p>'` across
`src/app/` returned nothing, and the ~750 "words" were ticker symbols, button
labels and panel headings. Auto Ads was additionally live site-wide, so Google
was placing units on `/privacy` and `/terms` as well.

**A calculator is not content.** That is the load-bearing sentence. A tool whose
screens carry only its own controls is a screen without publisher content no
matter how good the tool is, and no amount of engine work changes that. The fix
had to be writing.

What was done, and the constraints on changing any of it:

- `frontend/src/content/strategy-guides.ts` carries a distinct guide per slug —
  construction, closed-form max profit / max loss / breakeven, Greeks, when to
  use, failure modes, a worked example and FAQs. Every payoff identity is stated
  at expiry, **per share**, ×100 for one equity contract; the worked examples are
  arithmetic on the identity printed directly above them.
- **A template with a substituted name would reproduce the violation in longer
  form.** `strategy-guides.test.ts` asserts uniqueness on the prose fields
  (`lede`, `greeks`, `whenToUse`) individually — not on the whole record, because
  `netCost` and `outlook` are short enumerations that *should* collide.
- The home page had **no `<h1>` at all** and linked to none of the 26 pages: they
  were reachable only from `sitemap.xml`, because the in-app strategy picker
  changes client state rather than navigating. `SiteGuide` supplies both.
- `page.tsx` became a **server** component. This is a static export, so anything
  a crawler or a reviewer reads must be in the HTML the CDN serves rather than
  assembled after hydration.

### The two intents are two screens, and the ads follow the writing

The first fix put the article **below** the calculator on the same URL. Later the
same day the site was restructured onto tabs (`SiteNav`: Calculator | Guides),
which is the shape mortgagefvcalculator.com uses:

| route | carries | Google ads |
| --- | --- | --- |
| `/`, `/calculator/<slug>`, `/widget` | the tool | **none** |
| `/guides`, `/guides/<slug>` | the writing | yes |
| `/privacy`, `/terms` | policy documents | none |

**Moving the prose off the calculator screens while leaving the ads on them
would have recreated the violation**, so `/` and `/calculator/*` joined
`NO_AD_ROUTES`. What settled that direction was measuring the reference rather
than reasoning about it: **mortgagefvcalculator.com serves zero AdSense** — no
`adsbygoogle` anywhere on it — which is precisely why its own calculator screen
can be 730 words of pure interface. Ad-free is what makes a text-light tool
screen legitimate.

`/calculator/long-call` and `/calculator/iron-condor` are therefore **byte
identical again at 763 words**, which was the original violation and is now
correct: they are the tool and they carry no ads. `check-export.mjs` fails the
build if that stops being true.

The guides run **767 (protective-put) to 954 (long-call) words**, 26 of 26
distinct — *more* real content per ad-serving page than the 1251–1512 the
combined pages measured, because that figure was ~750 words of UI vocabulary
plus the article.

Each strategy has two URLs on purpose: "iron condor calculator" wants the tool
and "what is an iron condor" wants the article. Both are in `sitemap.xml` and
each links to the other — `guideHref` renders "How this strategy works →" in the
workspace header, and every guide links back with "Price a … on live market
data →". `check-export.mjs` asserts both directions, because a split that buries
one half is worse than no split.

**`NO_AD_ROUTES` must live in a plain module — `frontend/src/config/ad-routes.ts`
— and never in a `'use client'` one.** It sat in `AdSlot.tsx` for one build and
shipped this:

```js
if(undefined.some(function(r){ ... })){ ...pauseAdRequests=1 }
```

A server component importing a value out of a client module receives a
**client-reference proxy**, so `JSON.stringify` returned the literal `undefined`.
The guard then threw a TypeError while `<head>` was still parsing,
`pauseAdRequests` was never set, and Auto Ads would have served on every
protected route — three at the time, five now — **strictly worse than before,
because `/widget` had been correctly excluded.** Nothing in the source looked
wrong and no unit test could see it: a test importing the module directly always
gets the real array.

That is why `frontend/scripts/check-export.mjs` exists and why `npm run build`
runs it. It asserts the **emitted bytes**: the guard is present, well-formed,
free of `undefined` and names all five routes; **no tool or policy page carries
an ad unit**; every ad-serving page clears a word floor; no two guides render
identical text; the two intents are cross-linked in both directions; and every
JSON-LD block parses. Both directions are mutation-checked — duplicating a page
and re-introducing the `undefined` guard each fire it by name.

**That floor is 600, down from 1000, and the number moved because what it
measures changed — not to make a failing check pass.** The 1000 was set when an
ad-serving page was the calculator *with* the article below it, so roughly 750 of
those words were interface and it only ever demanded ~250 words of prose. The
guides are now the article alone, 767–954 words of which every one is publisher
content, against a flagged baseline of 753 words that contained almost none. The
floor sits below the thinnest real guide and far above any stub. Both figures are
recorded in the script's own comment, because a moved threshold with no stated
reason is indistinguishable from a moved goalpost.

Two suppression points enforce one rule and **both** are required.
`AdSlot` covers manual units; the inline `pauseAdRequests` script in the root
layout covers **Auto Ads**, which places units wherever it likes and ignores
anything `AdSlot` decides. Auto Ads is the half that is easy to forget, and
suppressing only one leaves the ads on the page. `/privacy` and `/terms` are
policy documents rather than publisher content — and they are the first pages a
reviewer opens to check whether the site discloses its advertising.

**Edge propagation is not a failed deploy.** Immediately after `wrangler deploy`
three of the 26 pages still measured 753 words while the other 23 measured
1251–1512. A cache-busting query returned the *new* content from the same URLs,
and a re-sweep a minute later showed all 26 updated. `wrangler.toml` already
records this behaviour for asset 404s; it applies to stale HTML too. Re-measure
before concluding an upload was missed.

`SponsoredBrokers` is unaffected throughout: it is this site's own affiliate
markup, not Google-served, so the policy does not reach it.

## Theme tokens and the workspace layout

Both were changed on 2026-08-16 and neither is derivable from reading the code
that consumes them.

**The green is mortgagefvcalculator's `#008154`, on both themes.** The accent
tokens already matched MFC exactly before this; the divergence a viewer actually
saw was elsewhere, and finding it took measuring which token paints what:

- **`--color-profit` paints 265 elements to `--color-accent`'s 23.** "The green
  is too light" is therefore a statement about the *profit* token — changing the
  accent alone would have altered almost nothing on screen. Both moved, plus
  `--color-call-tint` and `--color-atm-tint`, which had the old mint baked into
  their `rgba()` literals.
- **`#008154` is 3.83:1 on the dark ground — below AA's 4.5:1 for body copy.**
  Accepted knowingly, for chips and badges only, which is why `--color-profit`
  exists as a separate token from the text colours rather than being reused for
  prose.
- **A consequence, unresolved and flagged to the owner:** on the dark theme gains
  now read darker than losses, because `--color-loss` stayed bright. Rebalancing
  is a one-line change nobody has asked for yet.

**lightningcss drops a custom property that nothing USES, and the rule is use —
not value.** `--rgb-profit` and `--rgb-loss` were declared in `:root` and read by
nothing after the body wash moved to `--rgb-accent`; both vanished from the built
CSS, in both blocks. The tempting explanation — "the values were identical across
the two themes so it deduplicated" — is disproved by `--color-profit-dim`, which
is `#00764c` dark and `#00603e` light and was dropped just the same. The practical
consequence: **the body wash must interpolate `var(--rgb-accent)` and never a
literal**, or the gradient stops tracking the theme while every token still looks
right in the source.

**`--nav-h` is declared once (`2.25rem`) and consumed twice** — `SiteNav` sets its
own height from it, `StrategyWorkspace` is `calc(100vh - var(--nav-h))`.
Hardcoding either gives a workspace exactly one tab-bar too tall on a shell that
hides its own vertical overflow, so the bottom panel of every column is clipped
silently.

**A panel in a scrolling column needs `flexShrink: 0`.** Column 2 sets
`overflowY: auto` but its panels were the flex default `0 1 auto`, so the browser
compressed them to fit instead of letting the column scroll — measured at
1440×900 the ticket was squeezed to 308px against 327px of content and Position
was handed a 91px body. Nothing overflowed and nothing scrolled; the content was
simply cut. Every leg added made it worse, because the compression is
proportional.

**The 300×250 `AdSlot` in the chain column was never an ad.**
`NEXT_PUBLIC_ADSENSE_SLOT_RECTANGLE` is unset, so `AdSlot` fell to its
unconfigured branch — no `<ins>`, no slot id, a dashed placeholder — while
holding 250px in a column where the chain's own scrollable body had 204px and
showed 7 of 126 strikes. Removing it cost no revenue and roughly doubled the
chain. Measured after: chain body 204 → **462px**, probability distribution
180 → **274px**, Position 131 → **260px**.

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
- **Backend Deployment:** `scripts/railway_deploy.sh` — project `fearless-amazement`,
  service `options-calculator-backend`. It uploads with `curl` rather than
  `railway up`, because `railway up` enforces a ~30 s client deadline that
  Railway's own `/up` endpoint has been exceeding for any real payload; the
  script's header carries the three disproved hypotheses.

  **It checks the DESTINATION, and you should understand why before reaching
  for `railway up` instead.** The service comes from `~/.railway/config.json` —
  whatever `railway link` last pointed at — which is ambient state with no
  relationship to what is being deployed. This project has four services in one
  environment: the engine and three queue nodes. On 2026-08-12 the CLI was
  linked to **`sgee-queue-3`**, so a bare `railway up` from the repo root would
  have deployed the engine's `railway.json` (`backend/Dockerfile`,
  `numReplicas: 3`) onto a queue node — and Railway forbids volumes on a service
  with replicas, so it would not even have failed cleanly. The script now
  resolves the linked service id to its NAME and refuses anything but the
  engine's; `--service-name` overrides it deliberately. Both directions are
  exercised: it refuses `sgee-queue-3` and admits a matching name.

  **`railway logs --build` WITHOUT a deployment id shows the PREVIOUS
  deployment's log.** While a new build runs it prints the one you are
  replacing — `[3/3] Healthcheck succeeded!`, which reads exactly like success
  and cost 25 minutes of believing a deploy had landed. Pass the id the script
  prints. Then confirm the **cutover**, which no build log can tell you: a fresh
  boot sequence with one `model is LOADED` line per replica per assistant
  (3 replicas ⇒ 3 mortgage + 3 strategy), timestamped after the upload. A green
  healthcheck is not evidence the new image is serving.

  **`railway logs --service` has the SAME defect, and it is worse.** With
  nothing running it replays the LAST session's output — it does not say so and
  does not return empty — so any readiness check that greps its text passes
  against a dead container's scrollback. On 2026-08-12 the queue deploy script
  gated on a log line and reported *"all three nodes rolled, each proven up
  before the next"* while every deployment was still BUILDING and the two before
  them had FAILED their healthcheck. Nothing was proven.

  **Gate on `railway deployment list --service <svc>`** — the first data row is
  the newest deployment and its status (BUILDING / DEPLOYING / SUCCESS / FAILED /
  CRASHED) is Railway's own answer about *this* rollout. Read logs only with an
  explicit `railway logs --deployment <id>`. And note the corollary: **a marker
  chosen because "no previous binary could emit it" has a shelf life of exactly
  one deploy**, because that binary becomes the deployed one. It cannot be a
  gate.

  A build stuck at `scheduling build on Metal builder` with no further output is
  a Railway-side scheduling stall, not a slow compile — it can sit there for
  over an hour. Re-running the deploy gets it scheduled.

  **The stall's sharper tell is a deployment stuck at `INITIALIZING` with NO
  BUILD AT ALL**, which `railway logs --build <id>` reports as *"Deployment does
  not have an associated build"*. That one sentence separates "the compile is
  slow" from "nothing was ever scheduled", and only the second is fixed by
  re-uploading. It hit two of three queue nodes in one session on 2026-08-13,
  so treat it as common rather than exotic: if a deployment has sat at
  INITIALIZING for more than ~10 minutes, ask for its build log before waiting
  any longer.

  **A `railway up` timeout is a statement about the CLIENT.** The deadline is on
  this end of the transfer, so Railway can accept the upload and create a
  deployment while the CLI reports failure — and `deploy/queue-node/deploy.sh`
  used to call that FATAL, which STOPPED A ROLLING DEPLOY HALFWAY and left the
  cluster on mixed binaries with nobody told. It now checks whether a new
  deployment id appeared before retrying or giving up. Same lesson as the
  `imageDigest` discriminator: ask Railway about Railway.
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

## `import std;` and the one std.pcm

Every module interface unit in this tree — this project's `src/modules/*.cppm`,
sensen's 263, and SGEE's — says `import std;`. This is what
`docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md` §2 has always specified and what
C++23 standard modules means here. `SENSEN_NO_IMPORT_STD` and
`SGEE_NO_IMPORT_STD` were both FORCEd ON until 2026-08-12; they are now OFF, and
turning either back on is a decision, not a tidy-up.

**There is exactly ONE `std.pcm`, and that is the whole design.** sensen's
`std_module_precompile` builds it from `${CMAKE_CXX_FLAGS}` — which at that point
is `CANONICAL_FLAGS`, set before every `add_subdirectory` and therefore the
**union** of what every subproject compiles with. SGEE imports that same file via
`SGEE_EXTERNAL_STD_PCM` instead of building its own. A BMI mismatch needs two std
modules built from two flag sets; there is one, so it cannot arise. The old
justification for the shim — avoiding that mismatch — was answering the right
question with the wrong answer: build one from the union rather than none at all.

Clang enforces this strictly and the diagnostic is clear when you hit it —
`stack protector mode differs`, `compiled with the target feature '+avx2' but the
current translation unit is not`. Those name the flag. The failures below do not.

**Placement of `-fmodule-file=std=` in `backend/CMakeLists.txt` is load-bearing.**
`add_compile_options` applies only to targets created *after* it, so the block
sits between `add_subdirectory(sensen)` (which declares `std_module_precompile`)
and `add_library(sensen_slim)` (the first target here to compile a sensen
module), and still below `FetchContent_MakeAvailable(grpc)` so it never reaches
gRPC's ~3000 translation units. It was once ~500 lines lower and `sensen_slim`
missed it: seven TUs died on `fatal error: module 'std' not found` while
everything else built clean.

The flag says where the BMI is; it does not tell ninja to build it first.
`std_module_precompile` is `ALL`, which puts it in the default build but
establishes **no edge**, so under `-j` a module compile can be scheduled before
the BMI is written. Every target in the directory, and SGEE through its
`sgee_std_module` stub, depends on it explicitly.

**Four things a module does not give you, each of which cost a build cycle:**

1. **A module exports no MACROS.** `<cerrno>` (`errno`, `EINTR`) and `<cstdio>`
   (`stderr`, `stdout`, `stdin`) must stay textual. `import std;` supplies
   `std::fprintf` but not the stream you hand it.
2. **`import std;` declares `std::int32_t`, never `::int32_t`.** Unqualified C
   type names fail with `missing '#include <bits/stdint-intn.h>'`, which names a
   glibc internal header that is not the answer.
3. **`<new>` is an ODR ANCHOR and must be UNCONDITIONAL.** A `<new>` reached
   *transitively* — TBB headers are the live case — lands in the **global**
   module and is never merged with the std-module copy. Every allocation in a
   consumer then fails with `call to 'operator new' is ambiguous`, reported
   inside libc++, naming neither TBB nor the module that leaked it. Including
   `<new>` textually anchors the canonical declaration so the two merge.
   `sensen/src/tokenizer.cppm` carried a comment saying this above the include
   that proved it, and moving that include inside the guard removed the anchor
   from the one configuration that needs it.
   **Importing a module that anchors it does NOT inherit the anchor** — the line
   is repeated per translation unit (`options.cppm`, `pricing_engine.cppm`,
   `testing_framework.cppm`) rather than fixed once.
4. **A standard header in a global module fragment leaks into the BMI.** That is
   why sensen's guard exists and why 148 of its files had to be corrected: an
   include left *outside* `#if defined(SENSEN_NO_IMPORT_STD)` is textual on both
   paths, which is how (3) happens in the first place.

A header-std BMI and an `import std;` consumer **do interoperate** — `std::string`
from a textual header and from the std module are the same entity — so conversion
was incremental and a mixed tree is not broken. That is also why SGEE's
`replicated_queue_formal_verification_tests`, which sets `SENSEN_NO_IMPORT_STD`
on itself deliberately, still builds and passes.

Known residue: the vendored cpp23-logger still precompiles its own `std.pcm` and
`std.compat.pcm` from a smaller flag set. **Nothing consumes them** — every
compile binds `-fmodule-file=std=` to the shared BMI, which takes precedence over
`-fprebuilt-module-path` — so it is wasted build time, not a second BMI in use.

The 2026-07-30 investigation abandoned this because sensen's working tree had no
`std_module_precompile` target. It has one now, and libc++ is the standard
library here, so all three terms of its gate hold. See
`docs/session_logs/2026-07-30_libcxx_std_module_investigation.md`.

## Build Commands
- **Frontend Production Build:** `cd frontend && npm run build`
- **Frontend Dev Server:** `cd frontend && npm run dev`
- **Frontend Tests:** `cd frontend && npm test` (Vitest, `npm run test:watch` to iterate).
  Needs Node `^20.19.0 || >=22.12.0` — a floor introduced by vite 8, which
  vitest 4 pulls in. The build itself does not require it; the test suite does.
- **Backend Docker Build:** `docker build -t options-backend backend/`
- **Backend Tests:** `ninja -C backend/build build_tests && ctest --test-dir backend/build`

  **`ninja && ctest` is WRONG here and fails silently in the dangerous
  direction.** SGEE is embedded with `add_subdirectory(... EXCLUDE_FROM_ALL)`,
  which is correct — nobody wants 90 test binaries relinked on every engine
  build — but `ninja` therefore builds none of them while `ctest` happily RUNS
  whatever copy is on disk. An edit to a module under `backend/external/SGEE/src`
  leaves ctest executing yesterday's binary and reporting a pass.

  Measured on 2026-08-12: after touching one `.cppm`, plain `ninja` scheduled
  **0** SGEE test steps and `build_tests` scheduled **235**. It cost two full
  runs of chasing a failure whose source had already been fixed — `ninja` said
  "no work to do" while the binaries were older than the sources they came from.
  The reverse, a stale PASS over a real break, is the same mechanism with the
  outcome that does not announce itself.

  `build_tests` collects its dependencies from `BUILDSYSTEM_TARGETS` rather than
  a written list, because a list edited alongside every new `add_executable` is
  one that will silently omit the newest test — the one most likely to be
  failing.

### Frontend test suite

The frontend had **no test tooling at all** until 2026-08-10, and that is how a
defect on the primary user path reached production: picking a strike and
pressing Add, without touching the Expiry dropdown, committed a leg carrying a
fabricated `expiration_days: 0`, so payoff, P&L matrix, P&L surface,
probability distribution and Outcome all refused with "No expiration on any
leg" while that dropdown visibly showed a date. Nothing threw and nothing was
null — only a browser session could see it.

The suite is Vitest on the `node` environment, because the stores hold the
rules that decide whether a position can be priced and those rules are plain
functions over plain state. `src/test/grpc-harness.ts` is the entire seam: the
stores talk to `OptionsCalculatorClient` and nothing else reaches the network.

Two invariants there are worth knowing before editing a test:

- **Responses are hand-built objects, not generated message instances.** A
  generated `StrategyResponse` supplies a zero for every field nobody set, so a
  getter the store starts calling would silently read a plausible number. The
  hand-built builders fail by name instead. A default that looks like an answer
  is the exact failure this suite exists to catch.
- **The Pro gate is asserted on gRPC status code 7, never on message text**, and
  `entitlement.test.ts` deliberately varies the wording across three messages to
  pin that. The gate's copy was reworded twice in a single day; a store matching
  on text would fall through to the red "Unavailable" branch the moment someone
  improved a sentence.

`vi.mock` is hoisted, so each test file declares its own mock block at top
level — there is deliberately no shared `mockAmbient()` helper, because
wrapping the calls in a function would apply them to the harness rather than to
the caller.

### Overlapping `calculateStrategy` is the ordinary case, not an edge one

`PositionLegs` calls `calculateStrategy()` from its own change handler right
after `updateLeg`, and `StrategyWorkspace` fires it again from a `useEffect` on
`legs`. **One quantity keystroke therefore starts two requests and a
three-digit quantity starts six**, and nothing ordered the responses — the last
to resolve won. That put numbers computed for a position the user had already
edited on screen as the answer for the one in front of them, and it is the same
defect class as everything else in this file: a reading that looks like the
answer, taken from a layer that does not own it.

`useCalculatorStore` now carries the staleness token `useTreePricerStore`
already used (`calculationSeq`): every call bumps it at entry and commits
nothing if it has moved on. Four things about it are load-bearing:

1. **A precondition refusal bumps the token too.** Deciding there is nothing to
   compute must supersede what is in flight, or an older request lands a result
   for a position the user has since emptied.
2. **Because of that, every precondition return must also clear `isLoading`.**
   The superseded request used to be the only writer of `isLoading: false`; once
   it commits nothing, a refusal that overtakes it would spin forever.
3. **The catch is guarded as tightly as the success path.** A superseded failure
   does not merely show a stale message — it blanks `result`, and
   `StrategyMetrics` checks `gateDenied` and `modelLimit` *before* it renders
   `result`, so an overtaken `PERMISSION_DENIED` raised an upgrade prompt over a
   position that had just been priced successfully.
4. **The rate branch needs its own check.** It sits after the Treasury `await`,
   which is a window the post-RPC guard cannot cover.

`src/store/calculator-race.test.ts` drives resolution ORDER by hand — the
harness's `calculateStrategy` handler may return a promise precisely so a test
can make the older request answer second. Each guard is mutation-pinned to a
distinct named test. The `gateDenied` / `modelLimit` clear on the success path
is the one exception and says so in its comment: with the token in place no test
can fail without it, and it is written anyway so that `set` is complete on its
own terms.

`saveStrategy` and `loadStrategies` were deleted with it. They had no callers,
`loadStrategies` `console.log`ed its rows and discarded them — and both set the
same `isLoading` that five analytics panels read as "calculating", so saving a
strategy would have blanked all five behind a spinner while a Postgres insert
ran. `isLoading` now has exactly one writer, which is what those panels have
always assumed.

Leg ids were `Math.random().toString(36).substring(7)`, which keeps only what
follows `"0."` plus five characters — so a short mantissa gives a very short id
and an exactly-representable one gives the **empty string** (`(0.5).toString(36)`
is `"0.i"`). Roughly one id in five thousand came out under four characters.
`updateLeg` uses `map` and `removeLeg` uses `filter`, both on `l.id === id`, so
two legs sharing an id is one edit applied to both and one delete removing both,
silently. Now a counter — unique by construction for the life of the tab, which
is the only scope a leg id has.
