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

**RAILWAY'S EDGE STRIPS HTTP/2 TRAILERS. That is the whole mechanism, and this
section said something materially different until 2026-08-29.**

It read: *"Native gRPC does not survive the Railway ingress … no corresponding
request appears in `railway logs` — only gRPC-Web reaches the container."* The
second half is FALSE, and being false is what kept it unfixed: it sent every
reader hunting a routing fault that does not exist.

Measured against the live domain with `curl --http2`:

```
HTTP/2 200
content-type: application/grpc
x-envoy-upstream-service-time: 0        <- Envoy PROXIED it; upstream ANSWERED
body: -1798.651575458257198999          <- the correct closed-form payment
grpc-status: <ABSENT>                   <- the only thing missing
```

The request reaches Envoy, reaches the engine, and the engine returns the RIGHT
ANSWER. `server: railway-hikari` then drops the HTTP/2 trailers, and native gRPC
carries `grpc-status` **only** in trailers — so the client sees a DATA frame
with END_STREAM and no status and reports `Stream removed`. Nothing is
mis-routed and nothing is down.

The same request as **gRPC-Web succeeds through the same edge**, because
gRPC-Web frames its trailers INTO THE BODY where no proxy can strip them.
Verified — the response ends `0x80 00000f grpc-status:0`. That is what gRPC-Web
is FOR, and it is why the browser path never had this problem.

**NATIVE gRPC NOW WORKS AGAINST PRODUCTION.** `GRPC_NATIVE_PORT=50443` is set,
Envoy binds a second listener with TLS (ALPN `h2`), and a Railway TCP proxy
fronts it:

```
tokaido.proxy.rlwy.net:34513  ->  container :50443
```

Verified end to end, and the trailer is the proof:

```
HTTP/2 200
content-type: application/grpc
grpc-status: 0                      <- SURVIVES; the HTTP edge strips this
body: -1798.651575458257198999
```

**ALL FOUR SERVICES ARE ON IT, not just Finance.** There is one engine and one
Envoy, and the native listener routes `domains: ["*"]` / `prefix: "/"` to the
single `backend_grpc_service` cluster -- so enabling the endpoint once covered
optionsandfuturescalculator.com's own API as well as mortgagefvcalculator.com's.
Verified over the TCP proxy on 2026-08-29:

| service | native gRPC | evidence |
| --- | --- | --- |
| `sensen.finance.Finance` | ✅ | `ComputePayment` -> `-1798.651575458257198999` |
| `calculator.OptionsCalculator` | ✅ | `GetRiskFreeRate` 0.038625; `CalculateStrategy` maxProfit 10875, breakEven 587.25 |
| `mortgage.assistant.MortgageAssistant` | ✅ | reached; `PERMISSION_DENIED` from the Pro gate |
| `calculator.assistant.StrategyAssistant` | ✅ | reached; `PERMISSION_DENIED` from the Pro gate |

**Those two refusals are the point, not a gap.** A new PUBLIC port is exactly
where an entitlement gate would be bypassed if the gate lived in the proxy.
It does not -- `check_assistant_entitlement` and `check_strategy_entitlement`
run in the ENGINE, so they hold identically on gRPC-Web, on the JSON
transcoder and on this transport. Measured in both directions: a one-leg
`CalculateStrategy` is served anonymously and a two-leg one is refused
`PERMISSION_DENIED`, over the native endpoint.

The full `smoke_client … finance` suite passes against production over it,
which this file previously recorded as impossible. Run it as:

```
GRPC_DEFAULT_SSL_ROOTS_FILE_PATH=<tls.crt> \
GRPC_SSL_TARGET_NAME_OVERRIDE=grpc-native.optionsandfuturescalculator.com \
SMOKE_TLS=1 smoke_client tokaido.proxy.rlwy.net:34513 SPY finance
```

The certificate is SELF-SIGNED, deliberately: a Railway TCP proxy hostname is
not ours to obtain a public CA certificate for. It is a real TLS boundary --
`GRPC_ALLOW_PLAINTEXT` exists and is NOT used, because this port is reachable
from the public internet and authenticates with a long-lived API key header.

**A note on the Railway GraphQL API, because a wrong belief here cost time.**
This file said an `Unauthorized` from a write operation means the stored
credential is read-scoped. That was asserted again on 2026-08-29 and was WRONG:
the account token has full permissions, and `Not Authorized` came from sending
an EMPTY bearer. `~/.railway/config.json` stores it at **`user.accessToken`** --
`user.token` exists and is `null`, and there is no top-level `accessToken`, so
a fallback chain that tries those first silently yields "". Confirm auth with
`query{ me { email } }` before concluding anything about scope. The TCP proxy
was then created in one call with `tcpProxyCreate`.

**A DEAD TCP PROXY EXISTS ON THIS SERVICE and should probably be removed.**
`sakura.proxy.rlwy.net:56253 -> container :50052`. Nothing binds 50052 in the
engine container -- the connection is accepted by Railway and immediately reset
by the upstream (`errno=104`), no TLS, no ALPN. Harmless today, but 50052 is
SGEE's CONSENSUS port, and this file already warns against attaching a public
endpoint to an SGEE port. If anything ever binds it in this container it becomes
publicly reachable with no authentication.

Three ways to reach production through the HTTP edge, and one that cannot work
there by construction:

| path | works | why |
| --- | --- | --- |
| gRPC-Web | ✅ | trailers in the body |
| JSON transcoder (`POST /<pkg>.<Svc>/<Method>`) | ✅ | no trailers involved; `auto_mapping` covers every RPC |
| Railway TCP proxy | ✅ | raw TCP, no HTTP edge in the path |
| native gRPC via the custom domain | ❌ | edge strips the trailer the protocol requires |
| native gRPC via the TCP proxy | ✅ | **now live** — `tokaido.proxy.rlwy.net:34513`, TLS, trailers intact |

`smoke_client` now PRINTS this when pointed at the public domain, rather than
failing with `Stream removed` and leaving the reader to re-derive it. The
lesson generalises past this repo: **`x-envoy-upstream-service-time` in a
response proves the request reached the proxy and the upstream answered.** Its
presence alongside a transport-level error means the failure is in the RESPONSE
path, not the request path — which is the opposite of where the original
diagnosis looked.

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
device=cpu`, at every boot. `MODEL_URL` / `MORTGAGE_MODEL_URL` point at a
private artifact host, with `MODEL_SHA256` / `MORTGAGE_MODEL_SHA256` set
alongside them.

They are set on `options-calculator-backend` itself. A split fleet that moved
them to a separate worker was tried on 2026-08-22 and reverted the next day —
see "The split fleet was tried and REVERTED" below before concluding the
weights live anywhere else.

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
has not been compiled here. Every number above is CPU. The device-parity gates
(`test_gpu_batched_decode` Test 7, `test_gpu_argmax_softcap`) must run on a CUDA
build before that half is trusted.

**"No CUDA here" is a fact about THIS host, not about the project, and writing it
the other way has already produced a wrong conclusion.** The box this repository
is developed and deployed from — `oluwasanmi-fedora-server` — has an AMD
integrated GPU, no `/dev/nvidia*`, no `nvidia-smi` and no CUDA toolkit, so the
engine correctly builds `ENABLE_CUDA=OFF` and `sensen_slim` correctly excludes
every `qwen38` module. The MULTI-GPU SERVER is a different machine, and it is
where the CUDA builds, the QLoRA training runs and the Qwen3.8 work happen. A
CUDA-only change is therefore **not ungateable — it is gateable somewhere else**,
and the honest note on one is "needs the GPU server", never "compiled by nobody".

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

### "20% down" was priced as a 20% interest rate, and the gate said Proven

Reported from the app as "ParseOperation ignores the down payment", which is
true and is the less serious half. Probing four phrasings through the live
ingress on 2026-08-29 found this on the same utterance:

```
"amortization schedule for a 500000 home with 20% down at 6.5% for 30 years"
-> ComputeAmortization{annual_rate: 0.2000, loan_amount: 500000.00, ...}   200 OK
```

The model read the DOWN PAYMENT as the interest rate and discarded the stated
6.5%, and G3 admitted it because **20 -> 0.20 is an ordinary M2 candidate for a
rate slot**. A real field, a parseable value, every bound satisfied, and a 20%
mortgage priced. Grounding is per-field, so nothing asked WHICH percent.

**THE APP'S SUGGESTED FIX WOULD HAVE MADE IT WORSE, and the reason generalises.**
"The parser should emit `present_value = price - down`" was the right diagnosis
and, on that engine, would have turned a wrong answer into a REFUSAL: every
admissible map was UNARY, and 400000 appears nowhere in the utterance. That is
the "squeezed from both sides" failure this file records three times. **When a
client proposes a model change, check what the VERIFIER admits before agreeing.**

Two rules, pulling opposite ways, which is why they shipped together:

- **M0** — a percent the LEXER tagged as naming a down payment may not ground a
  **Rate** slot. Scoped to Rate and NOT Ratio: `down_payment_percent` on
  `ComputeClosingCosts` is a Ratio slot where 0.20 is exactly right. The same
  literal is correct in one slot and dangerous in another, which is why this
  could never be a magnitude heuristic. The existing closing-costs control,
  whose utterance says "10% down", is the regression guard.
- **M9** — the only BINARY map: `a - b` and `a x (1-p)` for a Money slot named
  `present_value` or `loan_amount`. Bounded deliberately — two field names, a
  literal the lexer already tagged, and a FALLBACK after every unary map fails.
  It adds exactly one candidate on the failing utterance, and a test asserts
  that a difference which is NOT a down payment stays ungrounded.

**The overflow guard I wrote first was wrong in the direction that hides
itself.** `p * pct` for a 500,000 house is 1e37 against a 1.3e36 ceiling, so it
rejected every realistic case and admitted only tiny ones — a guard that looks
conservative and is actually a silent feature-off switch. Splitting the multiply
into whole and fractional parts peaks at ~2e31 and needs no guard.

Both halves mutation-checked: deleting M0 reproduces the production symptom
verbatim (`Proven/None` on the 20% mortgage), deleting M9 reproduces both
refusals with their live message text.

**The corpus half is written and NOT retrained** —
`make_down_payment_extraction`, weight 0.045, both spellings, and six checks
pinning every spelling it emits against the lexer's word list (two lists, two
languages, two directories, nothing else connecting them). Until the retrain
runs on the GPU server the model still emits the gross price, so the live
outcome for "500k with 20% down" is an honest refusal naming the rate the user
actually stated — the direction to fail in, and better than a silent 20%
mortgage, but not the answer.

**A BYTE-IDENTITY GATE IS INVALID ON THIS RESPONSE, and it looks exactly like
the SGEE misrouting defect when you use one.** `FinanceParams.params` is a
protobuf `map<string,string>`, and a map has no defined iteration order — the
JSON transcoder emits its six keys in hash-bucket order, which varies between
processes and between runs. Measured 2026-08-29: 24 identical requests across
three runs produced **three different aggregate SHAs and one distinct answer**
after canonicalising with `sort_keys=True`.

That matters because the re-promotion gate recorded further down this file is
byte-identity across repeated runs, and applying it here reproduces the exact
signature of the lease-surface bug — same total, different bytes each run —
against a queue that is working correctly. **Canonicalise before hashing, or
compare the parsed object.** The strategy assistant has no map field and IS
byte-stable, so the two assistants need different gates.


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

- **THE MODEL OF RECORD IS v12 as of 2026-08-28** — QLoRA rank **64**, alpha 64,
  4 epochs, on the grounded corpus. It measures **400/508 = 78.7% raw params
  exact-match** on a 543-row holdout proven disjoint from every training set
  involved. It replaced v6 (`22c182e8…`), which measured **276/508 = 54.3%** on
  that same holdout: paired, v12 fixes **135** rows and breaks **11**.
  `ComputeRefinance` 9/38 → **38/38**, `ComputeDetailedAmortization` 23/48 →
  **48/48**, `ComputeHeloc` 27/30 → **30/30**, and `ComputeRentVsBuy` **0/18 →
  8/18** — the previous model could not serve that operation at all. Sole
  regression: `ComputeDepreciation` −1.

  **RANK 16 WAS THE CEILING, and every earlier score in this file was measured
  against it.** Holding the corpus fixed and varying only the adapter:

  | r16 | r32 | r64 | r16 + grounded corpus | **r64 + grounded** |
  | --- | --- | --- | --- | --- |
  | 297/508 | 338/508 | 386/508 | 308/508 | **400/508** |

  Rank is worth roughly **eight times** what the corpus fix is worth, and it is
  not memorisation: `ComputePresentValue` goes **0/11 → 11/11** on nothing but
  adapter rank, with no convention involved and no corpus change. At r16 the
  adapter could not represent that distinction AT ALL. The "trade" recorded
  further down this file — gaining one operation while losing two — was one
  capability displacing another inside a saturated budget, and both recover
  with rank.

  **Two external reviews rejected this and were wrong.** The strongest
  objection — "emitting a static minus sign is rank-1, so forgetting a
  100%-consistent constant is not dimensional exhaustion" — is locally true and
  globally wrong, because the adapter budget is SHARED across all 27
  operations. A nine-minute retrain settled what an hour of reasoning did not.
  **Measure before arguing; the retrain is cheaper than the debate.**

  **EVERY COMPARISON ON THIS HOLDOUT FAMILY BEFORE 2026-08-28 IS SUSPECT.**
  Each corpus revision shuffles and splits independently, so a row that is val
  in one is TRAIN in another: **304 of 600** rows in the corpus-B holdout were
  byte-identical members of the older model's train split. That contamination
  SUBSIDISED THE OLDER MODEL — on the full set the newer one looked like a wash
  (p = 0.17); on the 277 clean rows it was ahead 45 to 21, p = 0.0043.
  `eval_grpc_mortgage.py --assert-disjoint-from` is repeatable, once per model
  being compared, and REFUSES by default rather than warning.

- **The legacy figure, kept for the history:** it measured 62.5% params
  exact-match (325/520) through the real RPC, on a
  holdout whose labels are POSSIBLE. **Quote that number, not the 27.8% this
  line carried until 2026-08-20, and not `evaluate.py`'s.**

  The 27.8% was never a property of the model. It was measured against a corpus
  in which `phrase_money` rendered `round(v)` into the utterance while the label
  kept `f"{v:.2f}"` — so a stated "$1,825" had to become `1824.51`, which is not
  recoverable from the input. Eight operations were a hard **0/98** for two
  independently trained models, which is what proves it is the data. Fixing the
  generator and re-scoring the SAME deployed weights, unchanged, moved them to
  49/98 and the pooled figure from 49.0% to **62.5%**; refusals fell 214 → 135.
  The model was always better than the number; the number was measuring an
  impossible target. See `agent/dataset/build_mortgage_dataset.py`'s
  `phrase_money` docstring and commit `c6bcc62`.

  This is the third time in this file that a score turned out to describe the
  harness rather than the model — after the strategy assistant's `llama-cli`
  phantom and the `evaluate.py` bf16 gap. **A low score is a hypothesis about
  the model; confirm it is not a statement about the measurement first.**
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

## Security posture: what is enforced, and what is only claimed

Measured on 2026-08-28, not recalled. The distinction between "we observed it"
and "it is enforced" is the whole content of this section.

**TLS to Postgres is now ENFORCED.** libpq's default is `sslmode=prefer`, which
negotiates opportunistically and falls back to plaintext SILENTLY. Production
carried no explicit sslmode, so the TLSv1.3/TLS_AES_256_GCM_SHA384 it was
observed using was luck, not policy. `pg::Connection::connect` now merges
`sslmode` as a later `PQconnectdbParams` keyword -- the same mechanism it
already used to force `connect_timeout` -- so it beats anything in
`DATABASE_URL`. Default `require`; `PGSSLMODE_OVERRIDE` relaxes it for a local
socket-only dev database.

**`require` encrypts and does NOT verify the certificate.** It stops passive
eavesdropping, not an active MITM, and that limit is stated rather than implied.
`verify-ca` and `verify-full` were tested against production and both fail --
*"Either provide the file or change sslmode to disable server certificate
verification"* -- because the runtime image ships no CA bundle. `require` is the
strongest level that works today.

**RLS now covers every per-user table.** Before: `saved_strategies` rls=t
force=t 1 policy; `users`, `profiles`, `inference_jobs` all rls=f force=f 0
policies. Migration `06` extends the migration-04 pattern to `public.users` and
`public.profiles`.

**Proven to FILTER, not merely to be enabled** -- the same standard section 0 of
`test_strategy_store_pg` sets, because behaviour cannot otherwise see it:

```
as postgres (superuser, bypasses RLS)  -> 2 rows
as ofc_app, subject = user1            -> 1 row
that user's view of the OTHER row      -> 0
ofc_app with NO subject set            -> 0     <- fail-closed
```

Both tables are EMPTY today, so this closed a LATENT exposure. It stops being
latent at the first signup, which is the wrong moment to find out.
`inference_jobs` stays untouched: a shared work queue, not per-user data.

**There is a FIPS GATE and there is no FIPS CLAIM, and the gap between those is
deliberate.** `FIPS_MODE=off|preferred|required` (default `off`). `required`
loads the OpenSSL `fips` and `base` providers, pins default properties to
`fips=yes` so every implicit fetch resolves there, and **exits non-zero before
binding the listener** if any step fails. All four paths are exercised,
including the one that matters: with no provider reachable, `required` exits 1
and `preferred` starts and says so.

No message it emits contains "certified", "validated" or "compliant". The API
can read a provider NAME; a name is not a certificate. Four measured facts bound
every claim:

- **Stock `ubuntu:24.04` ships NO fips provider** -- its `ossl-modules/` holds
  only `legacy.so` -- so `FIPS_MODE=required` would refuse to start in
  production today. That is correct behaviour and why the default is `off`:
  this is the mechanism, not the claim.
- **Public TLS is Railway's edge, not ours.** `envoy.yaml` contains no TLS
  configuration whatsoever. No honest claim reaches past "cryptography inside
  the application container" -- a property of the architecture, not a gap more
  code can close.
- **The engine has exactly ONE TLS stack.** `gRPC_SSL_PROVIDER=package` is
  FORCEd in `backend/CMakeLists.txt` (originally to fix a segfault from two
  libssl symbol sets in one process), so gRPC links system OpenSSL rather than
  its vendored BoringSSL. `nm -D` shows zero defined `SSL_*` symbols. A boundary
  containing two crypto libraries cannot be reasoned about at all.
- **Every algorithm in use is already FIPS-approved** -- SHA-512,
  HMAC-SHA-512 truncated to 256 bits, HMAC-SHA-256, `RAND_bytes` -- and the
  whole surface is two files, `api_key.cpp`/`.cppm`. sensen performs no
  cryptography at all. The gap was never algorithm CHOICE; it is module
  VALIDATION, and that needs a validated module, not more code.

One non-approved primitive exists and is unreachable: cpp-httplib carries an
`EVP_md5` helper for HTTP digest auth, which no upstream this engine calls uses.
Under `fips=yes` that fetch fails rather than silently computing an MD5.

**HIPAA, PCI-DSS, SOC 2, ISO 27001 and FINRA: none, and none claimed.** No PHI
is processed. No card data touches this backend -- Stripe holds it, and no
column in the schema matches card/PAN/CVV/IBAN/SSN/routing patterns -- which is
*de facto* out of scope, not an attestation.

## Batch rent-vs-buy, and the ceiling that was never the engine

`ComputeRentVsBuyBatch` takes up to 1000 `RentVsBuyRequest`s in one round trip.

**The bottleneck for bulk work was never compute.** A single scenario is an
O(years*12) walk bounded at 1200 steps. What bounds a bulk caller is the
INGRESS: Envoy's local rate limit is `max_tokens: 100, tokens_per_fill: 10,
fill_interval: 1s` -- ten requests per second per replica -- so a large sweep
spends hours there and competes with live traffic throughout.

**It is not a quota bypass, and the charge is what makes that true:** it charges
`cost_default()` PER SCENARIO. It buys freedom from the rate limit and from a
thousand round trips, and no compute for free.

**Per-scenario outcomes.** One malformed row returns its own error -- verbatim
the message the single-scenario RPC would give -- and the other 999 still
compute. `results` is positional, so nothing is ever filtered out, only marked.

**ONE implementation.** The single-scenario body was extracted to
`compute_rent_vs_buy_one`, which both RPCs call, so the shape dispatch, the
compounding bounds and every refusal are identical. This file already carries
the scar of a dispatch that was correct for one caller and inverted for another.

Parallelised through `sensen::parallel` -- not `tbb::parallel_for` directly, for
the MSVC global-module reason `financial.cppm`'s own batch documents. Slots are
allocated serially then filled in parallel: `add_results()` mutates the field,
writing distinct elements does not.

| n | serial | parallel only | **shipped** |
| --- | --- | --- | --- |
| 1 | 136us | 151us | **103us** |
| 10 | 163us | 230us | **130us** |
| 100 | 686us | 446us | **519us** |
| 500 | 3275us | 1250us | **1244us** (2.6x) |
| 1000 | 5736us | 1826us | **1909us** (3.0x) |

**Parallel ALONE was SLOWER below ~64 scenarios** -- a scenario is only
microseconds of work, so scheduling is the whole cost. Shipping it
unconditionally would have traded a regression for every small caller against a
win only bulk callers see. Below 64 the loop is serial, which is why the shipped
column beats both. The residual ~136us is fixed gRPC/protobuf overhead,
irreducible from inside the RPC.

**The gate is byte-identity, not a timing:** the 500-scenario aggregate SHA is
`f22e8f083630c4e4` on both paths.

**Adding it failed the label-space drift check in THREE places** -- the
generator's `EXCLUDE_RPCS`, `test_mortgage_grammar` and
`test_mortgage_verification` -- which is the four-tables lesson working as
intended. `ComputeRentVsBuyBatch` is excluded from the assistant's label space
on purpose: it is a bulk API, not an utterance, and no sentence grounds a
thousand scenarios.

**On programmatic SEO, which is what prompted the batch RPC.** Generating tens
of thousands of pages from one template with substituted numbers is the EXACT
pattern that got this site flagged by AdSense on 2026-08-16, at four orders of
magnitude more scale. Worse, `check-export.mjs`'s duplicate check is EXACT
STRING EQUALITY (`byText`), so pages differing only in numbers are not
identical and would sail through green. `MIN_WORDS = 600` and the `/guides/`
ad ALLOWLIST are the guardrails that do still hold. Distinct numbers are not
distinct prose, and only the second is publisher content.

## Rent vs buy, and the zero that was not a silence

`ComputeRentVsBuy` carries TWO mutually exclusive request shapes in one
message: the legacy composite `monthly_piti_and_maintenance`, and the seven
granular fields the amortising model needs. On 2026-08-27 it refused **100% of
assistant traffic** — both label shapes, every request a model can emit —
while the test suite was green.

**The mechanism is that this service has two callers who spell the same intent
differently, and neither is wrong.** A JSON caller through
`grpc_json_transcoder` OMITS what it does not use, so the field arrives as
`""`. The assistant CANNOT omit: `mortgage_verification.cppm`'s G2b refuses a
missing key, so a model emits every declared field and says "not this shape"
with the convention value `0` — which `kConventionValues` exempts from
grounding *precisely so that it can*. The dispatch tested `.empty()`, which is
true for the first caller and never true for the second.

Three components said "emit a zero" and the fourth said "omit". The verifier's
own comment recorded the intent — *"These exemptions are what let the DEPLOYED
model keep parsing rent-vs-buy unchanged, with no retrain: it omits all seven"*
— and that is still true of v2. It stopped being true the moment a corpus
taught all sixteen fields. **The dispatch was built against a model that no
longer exists.**

The fix is a TOTAL FUNCTION over the decision graph, not another predicate:
`classify_decimal_field` (five signals, no parsing, so it cannot itself fail),
`join_group_signal` (malformed > magnitude > convention), and
`decide_rent_vs_buy_shape`, exhaustive over the pair. **Absent and Zero are the
same statement.**

**Symmetry is the fix, not the predicate.** An earlier round keyed the legacy
side on `is_positive()` and was correctly rejected: `""`, `"0"` and `"-100"`
fell through to the amortising model, which invented a 30-year 0% loan of
`price - down` and answered 200 OK. Tested symmetrically those same inputs make
both sides say nothing and reach the "neither shape" refusal. The earlier
attempt failed because it tested ONE SIDE.

**Why no test caught it: every test built its request the way a C++ caller
would.** Section 24 of `test_finance_service_validation.cpp` enumerates all 20
cells of the dispatch's input space and sends bytes taken verbatim from the
training corpus. It also asserts as an EQUALITY that v2 (which omits the seven)
and a retrained model (which emits them as zeros) get the identical legacy
answer — if those diverge, the convention stopped being a convention.
Mutation-checked twice, each reproducing a different production symptom.

A negative composite is now refused too; `-100` previously returned 200 OK with
a verdict computed from it.

## Closing costs (`ComputeClosingCosts`)

mortgagefvcalculator.com grew a standalone, itemised closing-costs screen, and
`finance.proto` had closing costs only as SCALAR FIELDS inside `ComputeRefinance`
and `ComputeHomeNpv`. The assistant's job is to name an operation in its label
space, so there was nothing to name: the failure was STRUCTURAL, not a weak
model, and the grammar correctly refused every invented name.

`sensen::calculate_closing_costs` reproduces the live site exactly — subtotal
15,335.96, cash to close 60,335.96, 3.41% of price — and that agreement is
between two independent implementations, which is the point.

Four things are load-bearing:

1. **The percentages do not share a base.** Origination and discount points are
   shares of the LOAN; title and transfer tax are shares of the PRICE. Mixing
   them yields a plausible figure rather than an error, so `check_finance`
   recomputes EVERY line against its own base — the sum identity sees a dropped
   term, never a wrong base.
2. **A credit reduces the TOTAL, not the itemisation.** Measured on the live
   page: a 5,000 credit left the subtotal at 15,336 and moved the headline to
   10,336. Hence both `itemised_subtotal` and `total_closing_costs`; folding it
   into a line would stop the itemisation summing to its own subtotal.
3. **`down_payment_percent` may be exactly 1.0** — an all-cash purchase, no
   loan, but title/appraisal/recording/transfer/escrow still owed. The first
   version refused it on a comment claiming the loan would go negative, which is
   false at exactly 1.0.
4. **`prepaid_interest_days` is `optional int32`**, the only explicit-presence
   field in the file. Absent means the 15-day convention; an explicit 0 means
   zero days, which a closing on the last day of a month owes. A sentinel
   mapping 0 to 15 made that unrepresentable. **Adding it silently broke all
   three text-level proto parsers**, which strip `repeated ` but not
   `optional ` — reading the presence marker AS the type and dropping the field
   from the drift comparison rather than erroring.

`validate_closing_costs` is split out so the service answers `INVALID_ARGUMENT`
for a bad argument while direct sensen callers get the identical refusal from
the engine. One contract, two callers. A credit larger than the bill is the
deliberate exception and keeps `FAILED_PRECONDITION`: 100,000 is ordinary on a
larger closing and wrong only relative to a subtotal that cannot be known until
the itemisation runs.

### An operation must reach FOUR tables, and the fourth gates dispatch

`kLabelSpace`/`kOperationIds` and the slot classifier in
`mortgage_verification.cppm`, `kConventionValues`, and **a fourth copy of the
label space in `mortgage_assistant_service.cpp`** that refuses the operation
AFTER the verifier admits it. Eight fields classified `Unclassified` until they
were added to `kMoneyFields`, and an Unclassified slot makes `translate()`
return Indeterminate — so every parse would have been refused, which looks
exactly like a bad model.

**A verifier LOOSER than the engine it guards is not a safety property.** Its
Ratio ceiling is 1.5 and the engine caps shares at 1.0, so it would prove a
parse the engine then refuses and the caller gets a transport error where an
honest refusal belongs. `kUnitCappedRatioFields` closes that; the global 1.5
stays, because an LTV above 1.0 is real on an underwater loan. The mirror
mismatch also existed: the verifier refused `tax_escrow_months = 0` while the
engine accepts it, and loans without escrow accounts are ordinary.

### The tests were not wired, and wiring them found seven defects

`test_financial.cpp` lives in sensen's tree, which this repo builds
`BUILD_TESTS OFF ... FORCE`; `smoke_client` needs a running engine and is not a
ctest target. **Zero of 99 ctest tests touched this operation** while coverage
appeared to come from three places. Section 23 of
`test_finance_service_validation.cpp` closed it and failed 7 checks immediately:
every engine-level refusal reached clients as `FAILED_PRECONDITION` rather than
`INVALID_ARGUMENT`, because `fail()` maps engine errors that way.

### The assistant was not broken. The CORPUS was, and it took three retrains to prove it

Held-out rows through the live ingress once scored **0/16 on closing costs and
0/24 across mixed operations**, and this section previously read that as a
broken model. It was a broken dataset, in three separate places, each of which
is the same defect: **a label carrying something the utterance does not
contain.** The model cannot extract what was never stated, and
`mortgage_verification.cppm`'s grounding gate then REFUSES what it invents — so
it is squeezed from both sides and learns to split the difference. That is the
documented "corrupted value" failure, and it is trained behaviour rather than
decode corruption.

The three, in the order they were found, with what each was worth measured
through the real `ParseOperation` RPC on the Q8_0 GGUF:

1. **`phrase_money` rounded away the cents the label kept.** A stated "$1,825"
   had to become `1824.51`. Eight operations were a hard **0/98** for two
   independently trained models. Fixing the generator and re-scoring the SAME
   deployed weights moved the pooled legacy figure **49.0% → 62.5%** with no
   retrain at all. Commit `c6bcc62`.
2. **`prepaid_interest_days` labelled the 15-day convention on utterances that
   never mention prepaid interest.** 21 of 26 held-out closing-cost failures
   were that field alone, every gold value 15 and every emission a number
   scraped from elsewhere (180, 30, 36, 150, 250).
3. **The closing-cost generator printed six decimal places for percents
   generated with `round(..., 4)`.** All 3270 labels ended in "00"; the model
   emitted the four-place convention it sees everywhere else, so rows that were
   numerically exact failed string equality — **0/42 string, 16/42 numeric**.

### Three retrains, and what each one actually proved

v4, v5 and v6 were all trained 2026-08-20 from `unsloth/Qwen3-0.6B`, QLoRA rank
16, 4 epochs, on the RTX 5090, and all measured against v2 on the SAME 520-row
legacy holdout, the same engine, one engine on `:50051`.

| | v2 (deployed) | v3 | v4 | v5 | **v6** |
| --- | --- | --- | --- | --- | --- |
| legacy raw exact / 520 | **325** | — | 320 | 317 | 313 |
| legacy served exact | **296** | — | 297 | 291 | 282 |
| closing costs, numerically correct / 42 | 0 | 7 | 16 | 15 | **39 (92.9%)** |
| closing costs served as params | 0 | — | 11 | 20 | **28** |
| vocabulary extension | none | 17 tokens, LoRA | 17 tokens, masked | 16 tokens, masked | **none** |

**v6 is the model to ship, and its legacy cost does not survive a significance
test:** paired McNemar against v2 on the row index is 40 lost / 28 gained, net
−12, **p = 0.182**. Against that it gains an operation v2 cannot express at all.

Three findings, each of which was a correct diagnosis of a real defect and two
of which were NOT the cause of the symptom:

- **The vocabulary extension WAS the legacy regression, and the mechanism is
  the parameter split rather than the tokens.** `--extend-vocab` calls
  `requires_grad_(True)` on the whole embedding, making it **155,582,464 of
  165,675,008 trainable parameters (93.9%)** and leaving the LoRA adapters at
  6.1%. Global gradient-norm clipping is then dominated by freshly-seeded rows
  and starves every adapter update. Removing it recovered the clean family from
  217 to 237 of 357, and trainable parameters to exactly **10,092,544**. The
  extension was never needed: the model spells `ComputeClosingCosts` as an
  ordinary BPE sequence, and v6's GGUF is byte-for-byte the same size as v2's.

  **A gradient mask does not fix this and measurably held while the regression
  happened** — all 151,669 pre-existing rows moved `0.000e+00`. The mask stops
  the weights moving; it cannot take them out of the optimizer.

- **An added token retokenises that string EVERYWHERE, including rows it was
  never meant to touch.** v4 added `annual_rate` because it is a
  ComputeClosingCosts field. It is also a field of six other operations —
  10,425 occurrences outside closing-cost rows, including the system prompt's
  own label-space listing — and HuggingFace splits added tokens out with a trie
  BEFORE BPE, so every one of them moved onto a single fresh row.
  `closing_cost_vocab` now excludes any candidate occurring outside a
  closing-cost row, tested as a SUBSTRING of raw row text rather than by
  JSON-key equality. Exactly one of seventeen is excluded.

  **This was a real defect and it was not the cause.** Removing it made the
  regression WORSE (v4 −23 rows, v5 −31), and the broken rows name
  `ComputeAmortization` — `ComputeDetailedAmortization`'s own sibling, nothing
  to do with retokenisation. Finding *a* bug is not finding *the* bug; the test
  is whether the number moves the way the mechanism predicts.

- **proto3 explicit presence is correct on the contract and useless as a
  label.** Omitting `prepaid_interest_days` is exactly what `optional` is for —
  absent means the convention, `value_or(15)` computes it. The model omitted it
  **0 times in 25 held-out rows whose gold omitted it**, 17 of them emitting a
  literal `0`, which is a different computation. 48% of training rows omitted
  the field; inference omitted it never. It fills every slot of a sixteen-field
  answer, so the only slot it can fill correctly is one the utterance grounds.
  Grounding it took closing costs 15/42 → **39/42**.

**Carried forward from v3, still true and not superseded by any of the above:**

- **A degenerate digit loop is NOT truncation, and raising the token budget
  makes it longer rather than fixing it.** Ten of v3's 42 closing-cost rows were
  scored as truncated and were not: the model emits a complete sixteen-field
  answer and then, at the final field, loops on `0` for 189 characters instead
  of closing the JSON. All ten land at 726-727 characters — the SAME total
  length — where a budget truncation cuts at an arbitrary point. There were
  **zero** such rows in 621 legacy decodes in the same run.
  `repetition_penalty` is pinned to 1.0 deliberately (1.1 compounded per
  occurrence and took params exact-match to 0/90), which leaves nothing damping
  a digit loop on the longest, most digit-dense output in the label space. Tell
  the two apart by the tail, not by the length alone.
- **v3's own regression was label BLURRING from putting LoRA on
  `embed_tokens`/`lm_head`.** 43 of its 63 newly-broken rows were
  `ComputeDetailedAmortization` (23) and `ComputeAmortization` (20), and 33 of
  63 named the WRONG operation. Paired McNemar p = 3.5e-05. A LoRA adapter on an
  embedding is a low-rank update to the WHOLE matrix, so every
  operation-name embedding moves. The masked variants in v4/v5 fixed that
  specific mechanism — and still regressed, for the parameter-split reason
  above.

**Unexplained and left open:** v6's formerly-poisoned family is 46/98 against
v5's 69/98. The vocabulary extension was helping those eight operations by ~20
rows while costing ~25 elsewhere, and that interaction is not accounted for by
anything above. It is the first thing to look at if this is picked up again.

### RE-PROMOTED 2026-08-20, and this time the queue is faster AND correct

`INFERENCE_QUEUE=sgee` is live again, after the root cause below was found and
fixed at the lease. The gate, on the same 16 rows that measured the defect:

| arm | run 1 | run 2 | run 3 | pattern |
| --- | --- | --- | --- | --- |
| new binary, `postgres` (control) | 11/16 | 11/16 | 11/16 | `.PPPPP.PP.P.P.PP` |
| new binary, **`sgee`** | **11/16** | **11/16** | **11/16** | **same pattern** |
| new binary, **`sgee`**, 16-way concurrent | 11/16 | 11/16 | — | same, **0 errors** |

Byte-identical to the local engine, to Postgres, and to itself across runs — the
reproducibility that was missing is back. And the queue now earns its keep:
**80 s against Postgres's 127 s** for the same sequential sixteen, and at 16-way
concurrency it absorbed every request in 20 s with **zero** `RESOURCE_EXHAUSTED`,
where a single local replica sheds 7 of 16 to its admission queue.

Cluster after the load, all three nodes: `last_applied == commit_index`,
`tick_errors 0`, `dlq_depth 0`, `apply_id_mismatches 0`, and `apply_rejections`
**0 and EQUAL across replicas** — the cross-replica comparison, not a comparison
against zero. Engine: **0 `[WARN]`, 0 `[ERROR]`, 0 foreign leases, 0
terminal-failure fallbacks.** That last group is the negative proof that matters:
the defence-in-depth branch never fired, so the broker is applying the filter.

**The deploy order was queue nodes first, engines second, and it is not
cosmetic.** All three nodes reported `live_tasks: 0` before anything moved, which
is the drain precondition — once the filter is live an untagged payload matches
no worker's filter and is handed to nobody. Nodes were rolled one at a time,
followers first and the leader last, so exactly one election happened.

**A control on the same binary came first, deliberately.** Deploying the new
engine while still on `postgres` and measuring 11/16 three times proves the code
change regressed nothing, so anything the flip changes is attributable to the
flip. Without it a good result reads as "the fix worked" and a bad one as "the
queue is still broken", and neither would be earned.

### The root cause, and why `INFERENCE_QUEUE=postgres` was the right holding position

**One variable, measured end to end on 2026-08-20.** The same 16 closing-cost
holdout rows, the same bytes, the same engine image, scored on which arm of
`ParseResponse`'s `outcome` oneof is set:

| arm | run 1 | run 2 | run 3 | row pattern |
| --- | --- | --- | --- | --- |
| local, `postgres`, 1 replica | 11/16 | 11/16 | 11/16 | identical every run |
| local, `postgres`, **375 concurrent background requests** | 11/16 | 11/16 | 11/16 | identical every run |
| production, **`sgee`**, 3 replicas | 4/16 | 7/16 | 7/16 | **different every run** |
| production, **`sgee`**, **1 replica** | 4/16 | 5/16 | 1/16 | **different every run** |
| production, **`postgres`**, 3 replicas | **11/16** | **11/16** | **11/16** | **identical, and identical to local** |

`.PPPPP.PP.P.P.PP` is the pattern local produces and the pattern production
produces on `postgres`. The two hosts now agree **row for row**, and production
went from a typical 6/16 to 11/16 — it was throwing away roughly **45% of the
model's answers**, not to a worse model but to the queue.

**`INFERENCE_QUEUE` is set to `postgres` on the Railway service as of
2026-08-20, and that is the fix.** The SGEE promotion recorded further down this
file is rolled back for the inference path. Do not re-promote it without
reproducing this table.

Four things about how this was found are worth more than the finding:

- **The elimination order was cheapest-first, and it mattered.** A 30-request
  periodicity probe (free, no config change) killed per-replica personality:
  lag-3 self-agreement **0.59 against 0.50 chance**, where three
  internally-deterministic replicas would give ~1.00. Then adding background
  load to the LOCAL engine — 375 concurrent requests sharing one prefix cache —
  killed batch shape and prefix-cache contamination together, because local
  stayed bit-identical through all of it. Only then was a live config touched.
- **Two external models were consulted and both were wrong, in the same
  direction.** Gemini 3.7 Flash High and GPT-5.3-Codex-High independently named
  shared prefix-cache / KV state under concurrent traffic, and both proposed the
  same decisive test: bypass the prefix cache in production. That would have
  cost a code change and a deploy to test a hypothesis the local control refutes
  for free. Both reasoned about what *could* break under concurrency; neither
  asked whether concurrency breaks it *here*.
- **`numReplicas: 1` was the test that pointed at the queue**, by exclusion. A
  single replica was MORE erratic (4, 5, 1), not less, so nothing about having
  three replicas explained it — which left the transport and the queue, and the
  queue is an env var.
- **ZERO `[WARN]` and ZERO `[ERROR]` throughout, on every SGEE run.** Every
  degrade branch in `SgeeAdmission` logs, so the fallback paths never fired.
  **SGEE was not degrading — it was returning wrong answers quietly.** That is
  strictly worse than a fallback, and it means "zero warns" proved the request
  was served by the cluster and proved nothing whatever about the answer.

**ROOT CAUSE FOUND: the SGEE lease is not partitioned by SURFACE, so the two
assistants steal each other's work.** The Postgres path carries the surface
through both halves and the SGEE path drops it entirely:

| | submit | lease |
| --- | --- | --- |
| Postgres | `PostgresAdmission(queue, **Surface**, local, deadline)` | `queue_->lease(**surface_**, worker_id_)` |
| SGEE | `SgeeAdmission(client, local, deadline)` | `client_.lease_blocking(worker_id_, visibility_ms_)` |

`SgeeLeaseSource`'s constructor takes no `Surface` and its `fill()` passes none
(`inference_admission.cpp`, `SgeeLeaseSource::fill`). `encode_prompt` writes
`{"prompt": ...}` and no surface tag, so the surface is not merely unused — it is
**not in the payload at all** and cannot be recovered downstream. Both services
then build a lease source against the SAME cluster, differing only in a worker
id (`::getpid() * 2 + 0` for strategy, `+ 1` for mortgage).

So the strategy assistant's owner thread can lease a MORTGAGE task, run that
prompt through the STRATEGY fine-tune, and complete it `OK`. The mortgage
grounding verifier then refuses parameters that came from a model trained on
option spreads — an honest refusal to a question that was never asked of the
right model.

It accounts for all four observations, which is what makes it the answer rather
than a candidate:

- **Zero `[WARN]`.** Nothing failed. The queue leased, the model decoded, the
  writeback completed, the RPC returned `OK` with a `Refusal` — every layer did
  exactly what it was built to do.
- **The failing row set changes run to run.** Which owner thread wins the race
  for a given lease is OS scheduling, so a different subset is stolen each time.
- **The total stays roughly constant.** Two workers poll, one is wrong, so about
  half of every batch is misrouted. Predicted 11 x 0.5 = 5.5; measured 4, 5, 7,
  4, 7, 7 — mean **5.7**.
- **One replica was no better** (4, 5, 1). Each process runs BOTH assistants, so
  halving the replicas does not change the ratio of wrong workers to right ones.
  This is exactly why `numReplicas: 1` failed to help and why that result
  pointed at the queue rather than at the fleet.

**The fix is to give the SGEE path the dimension the Postgres path already
has:** add `Surface` to `SgeeAdmission` / `SgeeLeaseSource`, write it into
`encode_prompt`, and make `fill()` release any lease whose surface is not its
own. Until then `INFERENCE_QUEUE=postgres` is the correct setting, and it is
what is deployed.

**Two external models found this independently and both were right** — Gemini
3.7 Flash High (via `agy`) and GPT-5.3-Codex-High (via `cursor-agent`), each
given the measurement and the file list and each reading the code themselves.
Both named the missing surface partition; both quoted the same two lines. That
is the opposite of the earlier consultation, where both were confidently wrong
about the prefix cache — and the difference is what they were given. The first
round got a prose description of symptoms and produced plausible mechanisms; the
second got **the isolated component plus the working control to diff against**,
and the control is what made it findable. "Diff the path that works against the
path that does not" is the instruction that turned two wrong answers into two
right ones.

**Do not read this as "SGEE is broken."** It is the queue for a *replicated task
log*, and the audit trail in `docs/SGEE_QUEUE_CLUSTER.md` is real work. What is
established here is narrower and completely certain: **routing model inference
through it costs 45% of the answers and all of the reproducibility**, and
Postgres was always the documented system of record and degrade target.

## Saved scenarios, and the row-level security under them

`SaveStrategy` / `ListStrategies` / `DeleteStrategy` on
`calculator.OptionsCalculator` let a Pro user name a position and reopen it
later. The stored unit IS a `StrategyRequest`, held as JSONB via protobuf's
`MessageToJsonString`/`JsonStringToMessage`, so the stored shape is the wire
contract and a field added to `StrategyRequest` needs no schema change.

**`Identity.id` could not be used to key the rows, and the reason is not
visible from reading it.** It falls back to the literal `"supabase-user"` when a
token verifies but carries no `sub`, and to `"licence"` on the licence path — so
two different people can share one `id`. It is also the KEY LABEL on the
API-key path (`acme-risk`), which names a site rather than a person, and
`KeyType` cannot separate those: a Supabase user and a publishable key are both
`Publishable`. Hence `Identity.subject`, set ONLY from a verified `sub`, empty
otherwise. Anything user-owned gates on it being non-empty.

**The subject check is NOT subject to `PRO_GATE_MODE`.** Every other gate in
`api_key.cpp` is commercial policy that Off/Warn may switch off; this one is
not. Without a subject there is no per-user key, and proceeding would write
every caller's scenarios into one shared bucket they could all read. Honouring
`Off` here would turn an entitlement switch into a data-leak switch. Gated by a
test that asserts UNAUTHENTICATED *with `PRO_GATE_MODE=off`*, mutation-checked.

**`calculator_service.cpp` still has ZERO reachable libpq symbols, and
`strategy_store` is split into two units to keep it that way.** The interface
(`strategy_store.cppm`) names no pg type and imports only `std`; `import pg;`
lives solely in `strategy_store.cpp`, whose imports are not re-exported. A
single-file module would have satisfied the compiler and silently destroyed
that invariant for every RPC in the file, pricing included. Verified on the
built objects, not by reading:

```
nm -uC calculator_service.cpp.o | grep -c 'pg::'  -> 0
nm -uC strategy_store.cpp.o     | grep -c 'pg::'  -> 13
nm -uC pg.cppm.o                | grep -c 'PQ'    -> 18
```

### The FK: migration 03 said "impossible", was WRONG, and 05 restores it

`01_init.sql` created `saved_strategies.user_id` as `UUID REFERENCES
public.users(id)` and asked for it to be "repointed at `auth.users(id)`".
Migration `03` argued that repoint was impossible because `auth.users` is in
Supabase's Postgres while this table is in Railway's, and a foreign key cannot
cross a database — so it made `user_id` TEXT and accepted "deleting a user no
longer cascades" as a necessary cost.

**The premise was false.** Auth here is SELF-HOSTED GoTrue on Railway
(`supabase-auth-production-c656.up.railway.app`, which the production frontend
is built against) running against **this** database. Measured:

```
SELECT nspname FROM pg_namespace ...   -> auth, public
SELECT to_regclass('auth.users')       -> auth.users
```

— and that is also why the role list contains `supabase_auth_admin` and
`authenticator`. There was never a boundary to cross. Migration `03` reasoned
from the PRODUCT'S NAME rather than from the connection string and wrote the
conclusion down as settled; its banner now carries a correction pointer rather
than being quietly edited, because the mistake is the instructive part.

Migration `05` converts `user_id` back to `uuid` and restores
`REFERENCES auth.users(id) ON DELETE CASCADE`. Deleting an account now reaps its
scenarios, with no webhook and no sweep — gated by
`test_strategy_store_pg` section 8, which saves two scenarios, deletes the auth
user, and asserts both are gone; mutation-checked by dropping the constraint.

Three consequences worth holding on to:

- **The `::uuid` cast in the policy RAISES on a malformed value** rather than
  matching nothing, so a non-uuid subject would surface as a database error
  instead of a fail-closed empty. `strategy_store.cpp` therefore validates the
  subject is a uuid BEFORE any statement runs — the cast is safe by
  construction, not by hope.
- **RLS refuses a foreign `user_id` before the FK ever sees it.** Rehearsed on
  the live database: inserting another user's row as `ofc_app` fails with "new
  row violates row-level security policy", and only bypassing RLS reaches the
  FK's own refusal. Outer guard first.
- **A deleted account with a still-valid token is a real state**, not a
  theoretical one — an access token outlives the account by up to an hour. That
  FK violation is mapped to `StoreError::UnknownUser` → `UNAUTHENTICATED`
  ("This account no longer exists. Sign in again."), because "temporarily
  unavailable, try again shortly" is the opposite of true for an account that is
  gone.

Migration 05 is guarded on `auth.users` existing, so a bare local database with
no GoTrue still applies cleanly. That guard is a place a control could silently
go missing, so section 0 of the test asserts the constraint EXISTS wherever
`auth.users` does. Its orphan cleanup runs UNCONDITIONALLY rather than only
inside the type-conversion branch — scoping it to the conversion made the
migration succeed once and fail every time after, which is the worst shape a
migration can have.

### RLS: `ENABLE ROW LEVEL SECURITY` alone would have been inert

The engine connects as `postgres`, and measured on the live database:

```
SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = current_user;
-> t | t
```

**Postgres always bypasses row security for a superuser or a BYPASSRLS role.**
`FORCE ROW LEVEL SECURITY` does not help — it only removes the TABLE OWNER's
exemption, never a superuser's. So enabling RLS and writing a policy would have
produced something `\d` displays and that filters nothing: worse than no RLS,
because it reads as protection.

Migration `04` instead creates **`ofc_app`, a NOLOGIN, NOSUPERUSER, NOBYPASSRLS
role**, and the store drops into it **per transaction** with `SET LOCAL ROLE`.
A superuser may `SET ROLE` to anything; inside that transaction `current_user`
IS the unprivileged role, so the bypass no longer applies and the policy
genuinely filters. It reverts at COMMIT/ROLLBACK, so a pooled connection is
never handed on with reduced privilege.

NOLOGIN is load-bearing: nothing ever connects AS `ofc_app`, so it needs no
password. That is what makes this work with **no new secret, no second
connection string, and no Railway env change** — the two alternatives (repoint
`DATABASE_URL`, or give the store its own URL) both need a password this
repository must not carry, and the first also serves the inference queue, so a
missed grant would take the assistants down rather than one feature.

The subject travels as a transaction-local GUC:

```
BEGIN;
SELECT set_config('app.current_user_id', $1, true);   -- bound param, is_local
SET LOCAL ROLE ofc_app;                               -- constant, no params
<the query>
COMMIT;
```

Three details are load-bearing. `set_config` runs BEFORE the role drop, so the
GUC is written by the privileged role. `is_local => true` makes the setting die
with the transaction — a session-level setting here would be a cross-user leak
with a very long fuse across a connection pool. And `SET LOCAL ROLE` cannot take
a parameter, which is exactly why the role is a compile-time constant while the
subject is bound.

The policy fails CLOSED: `current_setting(..., true)` is NULL when unset, and
`nullif(..., '')` collapses an empty subject to NULL too, so forgetting to set
it yields nothing rather than everything. `WITH CHECK` is present as well as
`USING` — `USING` alone would let a caller INSERT a row under someone else's
`user_id` and merely be unable to see it.

**Proven to be real defence in depth, not decoration.** Deleting the
application's own `WHERE user_id = $1` from `list` and `remove` and re-running
`test_strategy_store_pg`: **before RLS that mutation failed 3 and 6 checks; with
RLS all 27 still pass**, cross-user isolation included. The database alone
carries the guarantee.

`test_strategy_store_pg` section 0 asserts the POSTURE directly — `relrowsecurity`,
`relforcerowsecurity`, the policy's existence, and that `ofc_app` is neither
superuser nor BYPASSRLS — because behaviour cannot see it: the application's own
WHERE clause produces identical results whether or not RLS exists, so dropping
the policy would leave every other check green. Same lesson as the AdSense
denylist: assert what is in force, not what the code intends.

**`inference_jobs` is deliberately untouched** — a shared work queue, not
per-user data; RLS there would break both assistants for no confidentiality
gain. `public.users`/`public.profiles` likewise: both empty and unread, and
enabling RLS on a table with no policy denies ALL access, which would sit
dormant and then break whatever first used them.

Gates: `test_calculator_service` (62 checks, in-memory fake store, no libpq),
`test_strategy_store_pg` (31 checks, real Postgres, NOT registered with
`add_test` — returns 77 when `DATABASE_URL` is unset rather than passing
vacuously), and `saved-scenarios.test.ts` (12 checks). The frontend routes the
two refusals by gRPC STATUS CODE — UNAUTHENTICATED offers sign-in,
PERMISSION_DENIED offers checkout — never by message text, and the test varies
the wording while holding the code fixed, mutation-checked by transposing the
two codes.

**`buildStrategyRequest` is EXTRACTED, not duplicated.** Saving has to send
exactly what pricing sends or a reopened scenario prices differently from the
one saved; two copies would agree the day they were written and drift on the
first field added to `Leg`.

## State assumptions, and why RLS is the WRONG tool here

`RefreshStateAssumptions` / `GetStateAssumptions` on `sensen.finance.Finance`
carry the weekly US Census ACS refresh that used to live in the mortgagefv web
app. Fifty rows of per-state housing assumptions — median price, median rent and
a derived property-tax rate — behind fifty programmatic SEO pages.
`docs/STATE_ASSUMPTIONS_HANDOFF.md` is the client-facing contract.

**The table is in THIS database, and the handoff said otherwise.** It described
`public.state_assumptions` as already existing with the backend writing it —
true, but in the *app's* Supabase, for which this backend holds no connection
string. The only hosted-Supabase material in `config/.env` belongs to an
unrelated product and is not even a libpq URL. Migration `07` creates and seeds
it here. Reason from CONNECTION STRINGS rather than product names; migration 03
reasoned from the product's name once and wrote a false conclusion down as
settled.

**RLS is largely theatre on this table, and the reason matters more than the
conclusion.** Every other RLS migration in this tree protects TENANCY — rows
belong to a subject, scoped by it. This table has no tenants: fifty rows of
public Census aggregates that anyone may read. There is no confidentiality to
protect, only INTEGRITY. What actually does that work, strongest first:

1. **CHECK constraints** carrying the plausibility bounds. They bind EVERY
   writer including `postgres` and a hand-typed psql session — strictly stronger
   than an RLS `WITH CHECK`, which binds only policy-scoped roles. Measured:
   `median_price = 5` as superuser is refused.
2. **A column-scoped `GRANT UPDATE`** to `ofc_refresh` (NOLOGIN, NOSUPERUSER,
   NOBYPASSRLS) on exactly six columns. `insurance_annual`, `state_income_tax`
   and `note` are unwritable BY THE DATABASE, so a refresh cannot destroy
   hand-authored content even if this code tries. Proven: writing an editorial
   column as `ofc_refresh` is `permission denied`, as is DELETE.
3. **A role separate from `ofc_app`**, which gets SELECT only — a
   saved-strategies bug cannot write census columns, and vice versa.

**And the honest limit, which INVERTS saved_strategies.** There, forgetting the
subject GUC fails CLOSED. Here the connection is SUPERUSER, so a failed
`SET LOCAL ROLE` fails OPEN. `state_refresh.cpp` therefore refuses to run the
UPDATEs if the role drop errors — that refusal is the only thing making the
column grant mean anything at runtime.

**HIPAA does not apply and is not claimed.** Public aggregate Census housing
data about states: no PHI, no covered entity. Asserting otherwise would be a
false compliance claim, the same thing `fips_mode.cppm` refuses to make about
FIPS. What the intent DOES translate to is implemented: per-row provenance
(`data_source` / `data_year` / `refreshed_at`), a `job_runs` audit row, least
privilege, integrity at rest, and a key that is never logged.

### The first live run wrote NOTHING and reported success

Fifty rows validated, fifty UPDATEs issued, transaction committed,
`states_updated = 0`. The RLS SELECT policy named `ofc_app` and not
`ofc_refresh`, and **under RLS an UPDATE must first SEE the row.** Nothing
errored — an UPDATE matching zero rows is an ordinary UPDATE.

Two fixes, because one was not enough. The policy became `TO PUBLIC`, which is
what this data is. **And a zero-write run now rolls back and returns a
refusal** — the count was in the response the whole time, and a weekly job
reporting `ok` is one nobody reads. The success channel has to be wrong for the
failure to surface.

### The write was open to anyone, and quota is not authorisation

`RefreshStateAssumptions` is the ONE write on a service that is otherwise
entirely read-only, and it shipped with only a `CHARGE` in front of it — which
meters volume and authorises nothing. Found while writing the handoff sentence
describing which credential the admin button needs, and finding the honest
answer was "none".

**`data_year` is what makes that serious.** Every bound the validator enforces is
a PLAUSIBILITY bound, and a 2015 ACS vintage satisfies all of them — so an
anonymous caller pinning an old year rewrites all fifty states with decade-old
figures nothing downstream can distinguish from current ones. The rows would
carry honest provenance saying 2015, which no reader checks. Not a crash and not
a refusal: quiet, plausible, wrong data on the site.

Now **partner only** — not `authenticated`, not `pro`. The admin trigger is a
SERVER-side call from the app holding the issued partner key; a Pro subscriber is
a customer of the calculator, not an operator of it. Written as
`!_id.authenticated` the gate would pass an anonymous-vs-signed-in test and still
let every subscriber rewrite the site, so `test_state_assumptions_gate` exercises
the refuse direction with a VALID pro key, and is mutation-checked against
exactly that variant.

**It does NOT honour `PRO_GATE_MODE`**, for the same reason the saved-scenarios
subject check does not: every other gate here is commercial policy that Off/Warn
may switch off, and this one is an integrity control. It applies with more force,
because the blast radius is not one user's rows. `GetStateAssumptions` stays
open — public aggregates, nothing to authenticate.

### Design points that earned themselves

- **Candidate vintages are DERIVED from the clock**, not hardcoded. The handoff
  specified `[2023, 2022]`; derived candidates found **2024** — a full vintage
  newer — and the literal would have rotted every January with a failure that
  reads as an upstream outage.
- **An aborted run does NOT touch `refreshed_at`.** Bumping it on a run that
  wrote nothing makes every downstream staleness check lie — the LIVE-badge
  defect in a different costume.
- **Out-of-bounds values are REFUSED, never clamped**, and counted in
  `states_rejected`. A clamped value is a number nobody measured that nothing
  downstream can distinguish from one that was.
- **`refreshed_at` is a STRING, not a `Timestamp`.** An absent value must be
  distinguishable from `1970-01-01`, and with a numeric timestamp it is not.
- **The API key is a QUERY PARAMETER** (Census's design), so the fetch never
  logs the URL it built. `market_data.cppm` logs host+path on error, which is
  right there and would leak the key here; that shape is deliberately not reused.
- **A refusal travels as `OK` with `ok=false`**, not as a transport error — a
  caller must tell "the site is serving last week's numbers" apart from "the RPC
  did not happen", and a status code collapses those. `NotConfigured` is the
  exception (`FAILED_PRECONDITION`): an engine with no `CENSUS_API_KEY` is a
  supported build, exactly like an empty `MODEL_URL`.

### Scheduling: catch-up, not a calendar, on both replicas

A six-hourly tick asks "has a run succeeded in the last eight days?". A calendar
slot is missed whenever a deploy lands on the hour, and a missed weekly slot is a
week of staleness nothing reports; asking about the OUTCOME cannot miss.

Both replicas run the timer, made safe by `pg_try_advisory_lock` rather than by
electing a leader — Railway replicas share a hostname and have no stable
identity, as this file documents at length. The loser takes the `ABORTED` path.
The sleep is **interruptible**: `sleep_for` would hang a deploy for six hours.

### `finance_service.cpp` takes INJECTED hooks, and that was forced

`state_refresh`'s implementation unit imports `pg`, and `finance_service.cpp` is
compiled into two pricing test targets that deliberately carry no libpq. Merely
taking the ADDRESS of `state_refresh::run_refresh` there broke their link — so
`RegisterFinanceService` takes `StateRefreshHooks` and only `main.cpp` names the
implementation, the pattern `calculator_service` already uses with
`IStrategyStore`. The finance service is now testable without a database, which
it was not before. An unset hook is a SUPPORTED state answering
`FAILED_PRECONDITION`, not a crash.

Gates: `test_state_refresh` (19 checks, interface only, no libpq — with
`static_assert`s that fail the BUILD if the `constexpr` helpers stop being
constant-evaluable) and `test_state_assumptions_gate` (6 checks, a real
in-process server, both directions). The gate test is a SEPARATE binary because
`KeyRegistry` is a Meyers singleton reading `FINANCE_API_KEYS` once at first
use, so one process holds one key configuration for its life.

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

**Every number in that table is multiplied by the ENGINE's replica count, and
`railway.json` sets `numReplicas: 2`** (lowered from 3 on 2026-08-21 for cost —
see the memory section above) (raised from 2 on 2026-08-10). `callers_` is an in-process
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

**ROLLED BACK 2026-08-20, ROOT-CAUSED, AND RE-PROMOTED THE SAME DAY.**
`INFERENCE_QUEUE` is `sgee` again — see "RE-PROMOTED 2026-08-20" above for the
gate. The account below is what the rollback found, and it is kept because the
defect it describes is the reason the lease is now partitioned by surface.

**What the rollback measured:** serving
inference through SGEE cost 45% of the mortgage assistant's answers and all of
its reproducibility — production scored 4/16, 7/16, 7/16 on a fixed holdout with
a different row set each run, and 11/16 three times identically the moment the
queue was flipped back. See "RESOLVED: the SGEE queue path was the cause" above
for the full table and for why zero `[WARN]` did not catch it. Everything in the
rest of this section is the history of the promotion and is retained because the
defects it records are real; the promotion itself is not in force.

**`INFERENCE_QUEUE=sgee` was LIVE on the engine from 2026-08-13** — promoted,
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

## The split fleet was tried and REVERTED — the weights are on the engine

**`options-calculator-backend` carries both models on every replica, and that is
the intended state.** A split fleet — model-less engine replicas submitting to a
single `assistant-worker` that held the weights — was built, deployed and
verified working on 2026-08-22, then **reverted the next day**. The
`assistant-worker` service is deleted. `MODEL_URL` / `MORTGAGE_MODEL_URL` belong
on the engine.

It worked: the production holdout returned 11/16 on the identical
`.PPPPP.PP.P.P.PP` pattern from a fleet where no replica held weights, engine
replicas ran at **0.10 GiB** each, and the fleet was 2.05 GiB = $22/mo against
2.96 GiB = $31/mo with weights local.

**It was reverted because it halved parallel execution to buy $10/month.** Two
model-carrying replicas decode concurrently; one worker does not. Measured on
the 16-row holdout, concurrent wall time:

| | concurrent (16 requests) |
| --- | --- |
| 2 replicas, weights local | 32.0 s |
| split fleet, 1 worker | **65.7 s** |
| 2 replicas, weights local (restored) | 38.4 / 37.8 / 35.1 s |

A clean 2x when executors halved, and recovery when restored. **Do not
re-introduce a single-worker split without a throughput budget that says the
capacity is spare.**

**The methodology lesson is the more valuable half, and it was paid for twice.**
The decision to ship the split rested on a "latency cost" computed from ONE
sequential run before and ONE after (79.7 s -> 113.0 s, reported as ~1.4x). On
repeated runs the SAME configuration that produced 79.7 s measured 131.4 s,
138.8 s and 116.9 s — a 22 s spread across three runs, far larger than the
33 s the queue was being blamed for, and all of it ABOVE the split fleet's own
113.0 s. The
sequential figure does not track configuration and never supported the claim.
Correctness here was gated on repeated byte-identical runs; the performance
claim was n=1. **Apply the same evidence bar to a throughput number as to a
correctness one, or do not quote it.**

What survives and is worth keeping:

- `NoLocalBackend`, the submit-only paths, and `local_model_loaded()` are still
  in the tree and still correct — a replica with no weights CAN serve the
  assistant RPCs through the queue. Nothing enables it today.
- **`local_model_loaded()` vs `available()` is load-bearing regardless.** The
  first is "are the weights in THIS process", the second is "can this replica
  serve the RPC at all". The startup banner must read the first: it keyed on
  `available()` for one build, so a model-less replica logged `model is LOADED`
  and the documented cutover check `grep -c 'model is LOADED'` would have
  reported weights the fleet did not hold.
- **`ENGINE_GRPC_PORT`, never `PORT`.** The gRPC listener is configurable so two
  engines can run on one host — without it they both bind 50051 and
  `SO_REUSEPORT` makes the kernel SPLIT requests between them rather than
  failing the second bind. But Railway injects `PORT=8080` into this service and
  **8080 is ENVOY's listener**; Envoy proxies to the engine on 50051. Reading
  `PORT` would put the engine on Envoy's own port and leave Envoy proxying to
  nothing — a full outage for both sites, from a variable whose name means the
  opposite of what it looks like here.
- `deploy/assistant-worker/` is kept as a working template for a second engine
  service, with the two traps that each cost a build recorded in it:
  `backend/models/` must exist in the upload (the Dockerfile `COPY`s it — exclude
  only the `.gguf` files), and `BUCKET_ACCESS_KEY_ID` /
  `BUCKET_SECRET_ACCESS_KEY` are required or the image falls through to a
  `wget` the private bucket rejects and fails minutes later with a checksum
  error that never mentions credentials.

## Memory, and what the Railway bill is actually buying

**94% of the bill is RAM, and Railway's receipt states it in MB-MINUTES.** The
Jul 21 - Aug 21 2026 invoice read `Memory (per MB / min) Qty 331,691,526` and
$76.78 of an $81.37 total. That quantity is an integral, not storage:
`331,691,526 / 60 / 1024 = 5,399 GB-hours`, over 744 hours = **7.26 GB of RAM
held continuously**, at an effective **$10.58 per GB-month**. `Disk (per GB /
min)` and `vCPU (per vCPU / min)` are the same shape. Object storage — the line
that looks like the culprit — was **one cent**. Reading the big number as
gigabytes sends you at the volumes, which hold 4.3 GB between them and cost 87
cents.

Get the per-service split from the API, not the dashboard:
`usage(workspaceId, startDate, endDate, groupBy:[SERVICE_ID,PROJECT_ID],
measurements:[MEMORY_USAGE_GB,...])` at `backboard.railway.com/graphql/v2` with
the `accessToken` from `~/.railway/config.json`. **Its `MEMORY_USAGE_GB` is
GB-MINUTES summed across replicas; the `metrics(...)` query's identically-named
field is a FRACTION OF `MEMORY_LIMIT_GB` PER INSTANCE** — multiply by the limit
(32) to get GB. One field name, two meanings; reconcile against the invoice
total before trusting either.

### Every runtime knob is inert. All of them. Measured.

Boot RSS with both models loaded, before any request reaches the engine:

| arm | boot RSS | 16-way burst |
| --- | --- | --- |
| default (concurrency 4, context 4096) | 5,413.4 MB | 8.4 s |
| `MAX_CONCURRENT` = 2 | 5,413.3 MB | 10.1 s |
| `MAX_CONCURRENT` = 1 | 5,409.4 MB | 14.0 s |
| `CONTEXT_TOKENS` = 1024 | 5,419.9 MB | 8.4 s |
| `INFERENCE_THREADS` = 4 / 2 | 5,419.0 / 5,420.1 MB | 9.5 / 10.5 s |
| `QUEUE_DEPTH` = 2 | 5,420.4 MB | 3.1 s, **10 errors** |
| all three at minimum | 5,409.3 MB | 14.1 s |
| jemalloc tuned (`narenas:2,dirty_decay_ms:0`) | 5,270.5 MB | — |

Halving concurrency saves **0.1 MB** and costs 20% latency; taking it to 1 costs
66%. Shrinking the context window to a quarter *raises* RSS by 6 MB. **The paged
KV cache genuinely commits 16-token blocks on demand**, so the arithmetic that
makes it look enormous — 4096 tokens x 4 slots x 2 assistants — describes a
ceiling nothing reaches. Real sequences are ~400 tokens. Tuning jemalloc
recovers 2.6%.

**Do not reach for any of these to save memory.** This is the trap this file
warns about elsewhere in a different costume: a correct diagnosis of a real
mechanism that is not the cause of the symptom.

### The memory is the models, and they were held at 4x their own quantization

An engine with **no models loaded is 21.8 MB**. Everything else is the two
Qwen3-0.6B assistants, and the checkpoint's own tensor table says what they
should cost:

| tensor group | type | count | params | on disk |
| --- | --- | --- | --- | --- |
| `token_embd.weight` | Q8_0 | 1 | 155,582,464 | 157.6 MB |
| `ffn_gate` / `ffn_down` / `ffn_up` | Q8_0 | 84 | 264,241,152 | 267.6 MB |
| `attn_q` / `attn_k` / `attn_v` / `attn_output` | Q8_0 | 112 | 176,160,768 | 178.6 MB |
| norm gains | F32 | 113 | 65,536 | 0.2 MB |
| **whole checkpoint** | | **310** | **596,049,920** | **604.1 MB** |

**There is no `output.weight` tensor — `lm_head` is TIED.** Which made the boot
line `[QUANT-LMHEAD] output.weight kept quantized ... 165306368 bytes` a report
of work that should not have been happening:
`LlamaModel::try_load_quantized_lm_head` falls back to `token_embd.weight` when
`output.weight` is absent and then `memcpy`d it into a second buffer. So
`token_embd` was materialised **twice per model** — once dequantized to FP32 for
the embedding lookup, once copied as Q8_0 for the output projection:

| per model | was | is |
| --- | --- | --- |
| token_embd as FP32 embedding | 593.5 MB | 157.6 MB |
| token_embd as Q8_0 lm_head copy | 165.3 MB | 0 — shares the buffer |
| attention + FFN, Q8_0 | 446.2 MB | 446.2 MB |
| **live weights** | **1,205 MB** | **604 MB** |

Fixed in sensen `604d1d1c`, **on master since `7aa1d94e` (2026-08-25)** — it
shipped from the branch `perf/quantized-resident-embedding` for four days, which
is why older notes name a branch; that branch is merged and deleted:
`TokenEmbedding` gained a quantized-resident mode holding
the GGUF tensor's own bytes and dequantizing a row on first lookup, and the tied
`lm_head` points at those same bytes. **Boot RSS 5,420.1 -> 4,235.7 MB
(-1,184 MB, against -1,202 MB predicted from the table above).**

**The gate was byte-identity, not a score.** Dequantizing a Q8_0 row at lookup
is the same arithmetic through the same `dequantize_dispatch` the load-time
expansion called, so every logit — and therefore every emitted token — must
match exactly. 20 utterances across both assistants through the real RPC,
aggregate SHA `2cccb62e6a410ed9` before and after, **0 of 20 rows differing**.
A score that merely "held" would have hidden a real numerical change. ctest
100/100.

Two consequences worth keeping:

- **Measure after an idle soak, never at boot.** jemalloc hands back freed
  transient buffers on its own decay: the pre-fix engine drifted 2,730.9 ->
  2,238.2 MB per model over fourteen minutes, and two settled models came to
  ~4,476 MB — which is where production's measured 4.42 GB came from. A boot
  reading and a settled reading are different numbers.
- **`numReplicas` is 2** (from 3, 2026-08-21). Two still survives a single
  container loss and still clears the `restartPolicyMaxRetries: 3` trap that
  makes one replica dangerous; the recorded case for three was headroom, not
  correctness. Every quota number in the tier table below multiplies by 2 now,
  not 3.

**Do not requantize the checkpoint to save memory.** It is already 604 MB of
pure Q8_0 with nothing left to squeeze; the fat was entirely in how the weights
were loaded.

### The rest of it was 612 MB of duplicated RoPE tables, found by profiling

After the weights were accounted for, anonymous memory was still ~1,473 MB per
model against 604 MB of live weights. Seven hypotheses were measured and
essentially all of them died: `MAX_CONCURRENT`, `CONTEXT_TOKENS`,
`INFERENCE_THREADS`, `QUEUE_DEPTH`, jemalloc tuning (143 MB), the GGUF parser's
dequantization cache (**empty**), and the transient FP32 weight buffers the
MHA/FFN constructors zero-fill (~105 MB — the allocator was already recycling
those pages for the quantized buffers allocated moments later).

**The answer came from an allocation profile, not from another hypothesis.**
valgrind massif on a one-model boot put **612.5 MiB in `RotaryEmbedding`'s
cos/sin caches — 41% of the heap**, matching the arithmetic to the byte:

`RotaryEmbedding` holds `cos_cached_`/`sin_cached_` as
`std::vector<std::vector<float>>` of shape `[max_seq_len][dim/2]`, and
**`max_seq_len` is the GGUF's `context_length` — the model's TRAINED ceiling,
not the serving budget.** For Qwen3-0.6B that is 40,960, so one table pair is
40,960 × 64 × 4 B × 2 = 20.97 MB. `MultiHeadAttention` built **one
`RotaryEmbedding` per LAYER**, so 28 identical copies:

```
inner rows     40,960 x 64 x 4 B x 2 tables x 28 layers = 587,202,560 B
outer headers  40,960 x 24 B x 56 vectors               =  55,050,240 B
                                                          -----------
                                                          612.5 MiB
```

**This is also why `ASSISTANT_CONTEXT_TOKENS` measured inert** — that knob sizes
the paged KV cache, which commits on demand; it has nothing to do with this
table, which is sized from the checkpoint.

Fixed in sensen `65adac57`: `RotaryEmbedding::shared()` returns one immutable
instance per distinct `(dim, max_seq_len, base, scaling, yarn)` tuple. Sharing
is safe **by construction rather than by convention** — the pointer is `const`,
the tables are written once by `precompute_freqs_cis()` in the constructor, and
the class has no mutable state and no non-const member outside the constructor,
so concurrent readers on different layers' owner threads cannot race. Held by
`weak_ptr`, so the table dies with the last layer using it. Both assistants are
the same architecture, so the process holds **one** table set, not one per model.

Measured, both models loaded: RSS **4,184.1 → 2,942.1 MB**, anonymous
**2,945.9 → 1,703.7 MB (−42%)**. Byte-identical output (SHA
`2cccb62e6a410ed9`, 0/20 rows), ctest 100/100.

**Do not "fix" this by clamping `max_seq_len` instead.** It saves a similar
amount but is a behaviour change for long requests: `cfg.max_seq_len` also feeds
the sequence-length refusal checks and, on a CUDA build, the paged-attention
partition choice. Sharing has no such coupling.

**`SENSEN_QKV_FUSION=0` recovered nothing, and it is now FIXED.** This section
read "Known and NOT fixed" until 2026-08-25; the fix is sensen `dc51084b`.
`build_qkv_decode_plan()` interleaves Q+K+V into `qkv_fused_storage_` — a full
second copy, 124,780,544 B (119 MiB) per model, with `q_wq_`/`q_wk_`/`q_wv_`
retained. `qkv_fusion_enabled()` was consulted only where the plan is USED
(`multi_head_attention.cppm:2537`, `:3005`), never where it is BUILT, so setting
the flag to 0 removed the benefit and left the entire cost — byte-identical
resident memory with the flag set and unset. The fix is one early return at the
build site (`:3480`). **The default is still ON**, so nothing changes unless an
operator asks for it.

Measured, both models loaded, three runs each:

| | boot RSS | 20 requests |
| --- | --- | --- |
| fusion ON (default) | 2,942 MB | 19.8 / 19.9 / 19.9 s |
| fusion OFF | **2,703 MB** | 19.8 / 19.9 / 19.9 s |

−239 MB, and **the decode cost this was supposed to trade away did not appear.**
State the limit rather than the win: that is a SEQUENTIAL harness, and the
fusion exists to cut per-decode-step dispatch count, whose benefit would show
under CONCURRENT decode. The expected trade-off did not reproduce; do not read
the table as proof fusion is worthless.

**The remaining per-model anonymous memory is ~660 MB and is the actual Q8_0
weights.** It is irreducible without changing quantization.

### Pin the submodule to a BRANCH and master stops describing production

All four memory fixes above shipped to production on 2026-08-21 from
`perf/quantized-resident-embedding`, and `backend/sensen`'s pointer named that
branch tip rather than a commit on master. Nothing was broken by it — a
submodule pin is a commit id and works the same either way — but the
consequence is that **sensen's master did not describe what runs**, and a branch
carrying live code is indistinguishable at a glance from an abandoned
experiment.

Merged to master as `7aa1d94e` on 2026-08-25 and the branch deleted on both
remotes. Two things about that merge are worth keeping:

- **It merged with zero conflicts and that proved nothing.** Master had moved
  six commits, two of them in the same files — `1069a49f`'s paged-int8 KV pool
  fix and `e2056a3c`'s device-greedy repetition window, both in attention, where
  `65adac57` and `dc51084b` also live. Git merged them because they touched
  different LINES. The gate was the parent engine's build plus **100/100 ctest**
  against the merged tree, not the absence of conflict markers.
- **`ninja build_tests` does not build `calculator_engine`.** Its dependency
  sweep matches `^test_` only (`backend/CMakeLists.txt:2176`), which is correct —
  it exists to build what ctest runs. But it means a green `build_tests && ctest`
  can sit next to a **deploy binary that predates the change**: measured here,
  the sensen objects relinked at 07:35 while `calculator_engine` still dated from
  the previous evening. Run plain `ninja -C backend/build` as well before
  deploying, and check the binary's mtime rather than trusting "no work to do".

A C++20 module symbol also carries its module in the mangled name —
`sensen::RotaryEmbedding@sensen.rotary_embedding::shared(...)` — so
`nm | grep 'Class::method'` reports MISSING for every module-owned entity in
this tree. Grep the bare method name, or the count is a lie in the direction
that looks like a failed link.

**The lesson, since this file has now paid for it twice:** a plausible mechanism
with the right order of magnitude is not a finding. Seven of them were wrong
here. Profile the allocator before changing anything sized in gigabytes.


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

### The denylist was the wrong shape, and the 404 proved it

**On 2026-08-17 the site was still serving the violation, a day after the fix
above was deployed and believed complete.** Measured, not inferred:

```
GET /this-page-does-not-exist  →  404
                                  adsbygoogle loader        ✓
                                  enable_page_level_ads     ✓
                                  multiplex <ins>           ✓
                                  content: 13 words
```

Every dead URL on the site — and a crawler probing for `/wp-admin`,
`/index.php`, a renamed slug or a stale backlink generates many — served
page-level Auto Ads and a manual unit on "404: This page could not be found".
That is the notice's first and third conditions at once: a screen without
content, used for alerts.

**The mechanism was a denylist, and a denylist can only protect routes somebody
enumerated.** The root layout loaded `adsbygoogle.js` on every page and an
inline guard decided at runtime whether it could fill:

```js
if (["/","/calculator","/widget","/privacy","/terms"].some(r =>
      location.pathname === r || location.pathname.indexOf(r+'/') === 0))
  { ...pauseAdRequests = 1 }
```

**A 404's `location.pathname` is whatever the visitor typed**, so it matches no
entry. The guard was not broken — it was correct, it was tested, and it ran. It
simply had nothing to match. The error page is the one route nobody writes down,
and it is the route a crawler of dead links hits most.

**The fix is structural, not another guard.** The AdSense loader and the
page-level push now ship from `frontend/src/app/guides/[strategy]/layout.tsx`
and nowhere else — Next's documented way to load a third-party script on a
subset of routes (`node_modules/next/dist/docs/01-app/02-guides/scripts.md`,
"Layout Scripts"). A page outside that subtree carries **no Google ad code at
all**: nothing to suppress, and no runtime behaviour that has to be right.

Four consequences worth holding on to:

- **The segment is `guides/[strategy]`, not `guides`.** One level up would also
  cover the `/guides` index, which is a directory of 26 links — navigation, the
  policy's third condition. That one level is load-bearing.
- **`ad-routes.ts` is now an ALLOWLIST** (`AD_ROUTE_PREFIX = '/guides/'`,
  `adsOnRoute`). It fails the other way: an unanticipated route gets no ads
  until somebody decides it should, which costs impressions and cannot cost a
  policy strike. `AdSlot` uses it as defence in depth, not as the mechanism.
- **Site verification is unaffected**, which is why removing the loader from the
  shared layout was safe. `<meta name="google-adsense-account">` stays in the
  root layout on all 59 pages; it asserts ownership, the loader requests ads,
  and they are separate tags.
- **`src/app/not-found.tsx` now exists.** There was none, so the export shipped
  Next's default one-sentence page. It renders real orientation — both tabs and
  five popular strategies, each linked to its guide and its calculator — and is
  `robots: { index: false }`. It carries no ads regardless, by construction.

**The tests passed throughout, and that is the lesson worth carrying past this
site.** `ad-routes.test.ts` asserted all five denylisted routes, all 26
calculator pages, and the near-miss cases. Every assertion was true. They were
written from the same list as the code, so they could only ask whether the
enumeration was *implemented* — never whether it was *complete*. The rewritten
file asks the complementary question first: what does an **unanticipated** route
get? Six 404 paths are asserted by name, mutation-checked against the old
predicate.

`check-export.mjs` changed the same way. It used to verify the guard was present
and named all five routes — which it was, and did, on the 404 as well. It now
sweeps **every `.html` in `out/`** rather than a hand-written page list, and
asserts that ad code (loader, page-level push, `data-ad-slot`) appears on the 26
articles and on nothing else, plus that it has not *vanished* from the articles.
A restored `pauseAdRequests` string fails the build by name, because its return
would mean the denylist came back.

Verified live after deploy: `/`, `/calculator/*`, `/guides`, `/widget`,
`/privacy` and three separate 404 paths all carry `loader=0 push=0`;
`/guides/<slug>` carries `loader=1 push=1`.

### The client-module trap that created `check-export.mjs`

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

**That two-suppression-point design is gone, and do not reintroduce it.** It
was `AdSlot` for manual units plus an inline `pauseAdRequests` script for Auto
Ads, both reading the same denylist — two runtime guards that both had to be
right, on every page, for a page to be safe. Now the ad code is only emitted
where ads belong, so there is one rule and no suppression at all. `/privacy` and
`/terms` remain ad-free for their own reason: they are policy documents rather
than publisher content, and they are the first pages a reviewer opens to check
whether the site discloses its advertising.

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

  **`scripts/railway_deploy.sh` was itself recommending the broken command**
  until 2026-08-25 — its closing instructions said
  `railway logs --service <svc> | grep -c 'model is LOADED'` for the cutover
  check, which is exactly the replay described above. It now names the
  deployment it just created. A script that prints the wrong verification step
  is worse than one that prints none, because the reader has no reason to doubt
  it. The same block also hardcoded "numReplicas 3 => 3 mortgage + 3 strategy"
  for four days after `numReplicas` dropped to 2, so the honest gate of 4 lines
  read as a shortfall against a stated 6; the count is now derived from
  `railway.json`.

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

## sensen builds without CUDA, and the slim list is hand-maintained

As of 2026-08-25 sensen configures, compiles, links and tests on a host with no
NVIDIA GPU and no CUDA toolkit — `ninja -k 0` exits 0 with 368/368 targets on
`oluwasanmi-fedora-server`. It did not before: four separate stages failed
(CMake generate, dependency scan, compile, link), each from CUDA-only code
reachable from code that is not, and the fifteen CUDA-only tests and examples
are now guarded so ctest reports **Skipped (77)** rather than the target being
absent.

**This project never saw any of it, and that is the trap.** `backend/` builds
`sensen_slim`, whose module list is written out by hand in
`backend/CMakeLists.txt` and excludes every qwen38 module — which is where most
of the CUDA leakage lives. A defect can therefore make sensen unbuildable
standalone while this repository stays green for weeks.

**The hand-maintained list cuts the other way too.** `kv_cache.cppm` began
importing `sensen.kv_lowbit` with the sub-8-bit KV work, and `sensen_slim` failed
with `module 'sensen.kv_lowbit' not found` the moment the submodule pointer moved
— its two dependencies were already listed, only the new module was missing.
Every new module that a listed module imports has to be added alongside it;
nothing derives this.

**A CUDA-only test must SKIP, not vanish and not crash.** Guarding the source
with `#ifdef SENSEN_HAS_CUDA` and giving it `int main() { return 77; }` keeps the
target present and reports Skipped, which is the convention
`tests/CMakeLists.txt` already documents for "missing model / no CUDA device". A
test that disappears from the list is indistinguishable from one that was never
written. Four tests from the same upstream work still ABORT or SEGFAULT rather
than skipping (`test_lowbit_flash_{cutile,cublas,graph}`,
`test_linear_attention_distributed`) — they build, so that is a runtime guard
still to be added.

**A plain `cmake -B build -S .` now does the right thing on a CPU-only host.**
Until sensen `ea8222fb` (2026-08-26) the auto-detector was dead code: the file
opened with `option(ENABLE_CUDA ... ON)`, which writes a cache entry, so the
detector 470 lines later asking `if(NOT DEFINED ENABLE_CUDA)` was always
answered "defined" and took the `else()` branch. `ENABLE_CUDA` was hardcoded ON
everywhere, and a toolkit-less host died with `No CMAKE_CUDA_COMPILER could be
found` — the exact failure the dead code existed to prevent. The probe also now
asks for a **compiler** rather than a GPU: `project(... LANGUAGES CUDA)` needs
nvcc, not a device, so `nvidia-smi -L` was the wrong question. Verified here:

```
-- ENABLE_CUDA default: OFF (no CUDA compiler found)
--   ENABLE_CUDA:     OFF
```

The five tests that ABORTED rather than skipping on a CPU host
(`test_lowbit_flash_{gpu,cutile,cublas,graph}`, `test_qwen3_14b_flash_attn_smoke`)
are guarded at the source and carry `SKIP_RETURN_CODE 77` in the same commit.
Their old comment claimed a CPU-only build "falls back to CPU and the comparison
is trivially exact"; it does not — each names its backend explicitly and
`LowBitFlashAttention` throws on a GPU request without CUDA.

## The toolchain is clang 23, and what moving to it found

`ARG LLVM_VERSION=23` in `backend/Dockerfile` and `backend/Dockerfile.queue-node`
as of 2026-08-29, up from 22. `config/cpp_details.txt` rules 50/60 say
`clang++-23` now.

**Gated on the container, not on the local build**, because the container build
is what deploys. `podman build --build-arg LLVM_VERSION=23` succeeds, uses
llvm-23 in 378 places and llvm-22 in **zero**, and the resulting image passes the
whole `smoke_client … finance` suite in-image — every independent identity, with
`pmt(300k, 6%, 30y) = -1798.651575` matching the closed form exactly. Locally:
**ctest 103/103 on clang 23**, identical to clang 22, with **zero** compile
errors in this tree, sensen, SGEE, gRPC or protobuf.

**The reason to move is clang-tidy, and it is a big one.**
`backend/CMakeLists.txt` forces `SKIP_CLANG_TIDY ON` "due to c++23 module
scanning bugs". Measured A/B on one module, each compiler with its OWN
`std.pcm` (a mismatched BMI makes this comparison meaningless — the first
attempt scored clang-tidy 22 against clang 23's `std.pcm` and got four bogus
"module file uses a newer format" errors):

| | diagnostics on the same file |
| --- | --- |
| clang-tidy 22 | **11,176** — it walks into libc++'s own `std.cppm` internals |
| clang-tidy 23 | **5** (4 more correctly suppressed as non-user code) |

It also analyses a real 2,400-line module of ours (`mortgage_verification.cppm`)
producing 23 actionable diagnostics, no crash, no flood. **That does not
re-enable `CXX_CLANG_TIDY` on its own**: measured, **0 of our `.cppm` files
appear in `compile_commands.json`** in either build, because CMake does not emit
database entries for `FILE_SET CXX_MODULES` sources. That is a generator
limitation, not a clang-tidy one, and clang 23 does not change it — a wrapper
passing flags directly is what works today.

**llama.cpp did NOT build against libc++ 23, and is now PATCHED rather than
bumped.** 221 errors at pin `b6963` reduced to **thirteen sites in seven files**,
all missing transitive includes (`getenv`, `atoi`, `strtol`, `std::max`) that
libc++ 23 stopped providing implicitly. Ordinary missing-include bugs that
libstdc++ and older libc++ were hiding; nothing llama.cpp-specific.

`backend/patches/llamacpp-b6963-libcxx23-includes.patch` adds seven `#include`
lines and is applied by `FetchContent_Declare`'s `PATCH_COMMAND`. **A patch, not
a `GIT_TAG` bump, and that is the whole point:** the pin is what makes the parity
probes meaningful — llama.cpp is here to be an INDEPENDENT implementation, so
moving it is changing the reference. Seven `#include` lines change no behaviour
and no number, which a version bump cannot promise. Proven against a pristine
clone of the pin, and proven idempotent: `git apply` succeeds on a clean tree,
and the `--reverse --check` fallback succeeds when it is already applied, so a
re-configure over a populated source is not an error.

**It also now compiles at C++23** (`LLAMACPP_CXX_STANDARD`, default 23, up from
17). The cost the old C++17 pin existed to avoid is real and was counted rather
than dismissed: C++23 turns CMake's Ninja module scan on for these sources, and
`build.ninja`'s llama/ggml scan steps go **1 → 486**, the tree's total **3,639 →
4,124 (+13%)**, for ~200 translation units containing no `import` and no
`export module`. Accepted knowingly — it buys one standard across everything
this repo compiles, the cost lands on configure/scan rather than the deploy
image, and `-DLLAMACPP_CXX_STANDARD=17` reverts it in one flag.

**What was NOT done, deliberately: llama.cpp's sources are not restyled to this
repo's house rules** — trailing return types, `[[nodiscard]]`, `std::expected`,
`import std`, no raw pointers. Those govern code we own. Restyling ~200k lines of
upstream would erode the independence that justifies vendoring it at all, and
would have to be redone on every bump. Compiling it at our standard is the part
of "C++23 compliance" that is both meaningful and free of that cost.

Four arms gated, all **103/103**: clang 23 with llama.cpp off, on at C++17, and
on at C++23; plus clang 22 with the patch and C++23, from while the local
toolchain was still 22. The whole block sits inside
`if(ENABLE_LLAMACPP_BACKEND)` — OFF in both images — so none of it reaches the
deployed binary. Method and rationale: `docs/technical/LLAMACPP_LIBCXX23.md`.

**The LOCAL toolchain is clang 23 too, as of 2026-08-29** — `/usr/local` was
overlaid with the LLVM 23.1.0 release, so `clang++`, `clang-tidy`, `clangd`,
`lld` and libc++ (including `share/libc++/v1/std.cppm`) are all 23 and local
builds no longer differ from the image. It is an OVERLAY, not a wipe: the
tarball writes only its own files, so cmake, ninja, ctest, TBB and Z3 in
`/usr/local` are untouched. The clang 22 tree it replaced is at
`~/usr-local-llvm22-backup.tar.zst` (5.1 GB, `tar -C /usr/local --zstd -xf` to
roll back) — a from-source LLVM is hours to rebuild, so the rollback had to be a
file rather than a procedure.

**An OVERLAY install of a compiler is not a clean one, and the difference reads
as a broken release.** The first rebuild after the move produced **700+ errors,
every one INSIDE libc++ itself** — `"If libc++ starts defining <ctype.h>, the
__has_include check should move to libc++'s <ctype.h>"`. libc++ 22 shipped four
C-compatibility headers (`ctype.h`, `inttypes.h`, `float.h`, `fenv.h`) that
libc++ 23 REMOVED; an overlay adds and overwrites but never deletes, so those
four sat shadowing the C library's and libc++ 23's own `<cctype>` tripped its
guard. Counted: **1,701 files locally against 1,688 upstream** — thirteen
strays, four fatal. Fixed by replacing `include/c++/v1` wholesale rather than
merging into it; `share/libc++` and `lib/x86_64-unknown-linux-gnu` were diffed
the same way and were clean. **Diff the file lists after any overlay.**

**`rm -rf backend/build` is not free.** It also removes `_deps/grpc-src`, which
the other build directories reference via `FETCHCONTENT_SOURCE_DIR_*` — one
build tree is load-bearing for the others, and deleting it forces a full gRPC
re-clone and rebuild.

**Pass the REAL compiler path, never the ccache shim.** `which clang++` resolves
to `/usr/lib64/ccache/clang++`, and CMake recording that as
`CMAKE_CXX_COMPILER` sends sensen's
`dirname(dirname(compiler))/share/libc++/v1/std.cppm` walk to
`/usr/lib64/share/...`, which does not exist — the same failure the Dockerfile
comment describes for `/usr/bin/clang++`. Configure with
`-DCMAKE_CXX_COMPILER=/usr/local/bin/clang++` and
`-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`, which keeps the cache without breaking
the walk.

### Two latent bugs the second toolchain exposed, both ours

**Neither is a clang 23 defect.** A second compiler is a differential test, and
these are what it found.

**1. libc++ was located by guessing absolute paths.** SGEE and sensen each
walked a fixed list of directories and fell through to a hardcoded
`/usr/local/lib/x86_64-unknown-linux-gnu` — a DIFFERENT compiler's runtime. A
relocatable LLVM release installs its runtimes at `<root>/lib/<target-triple>/`,
which no entry matched. Objects compiled against libc++ 23 headers then linked
against libc++ 22 and failed with

```
undefined reference to `vtable for std::__bad_variant_access_with_msg'
```

which names an internal new in that release and **reads as a corrupt standard
library rather than as two of them**. Configure printed a path and no warning,
and the ENGINE linked fine — only SGEE pins a path, so it surfaced only in
SGEE's own tests. Both now ask `-print-file-name=libc++.so`, so the libraries
cannot disagree with the headers that compiled the objects. This is what rule
114 already required.

**2. The neo4j driver's "auto-registration" was link-order luck.** A
namespace-scope object registers the driver in its constructor, and no consumer
names a symbol in that translation unit — **`import` makes declarations visible
and creates no link-time reference**. In a static archive the linker therefore
has no reason to extract the member, and an initializer in an unextracted member
never runs. It worked only while some OTHER symbol there happened to be needed.
Measured on identical source: `graph_tests` linked with clang 22 carries **15**
`Neo4jConnection` symbols and passes; with clang 23 it carries **ZERO** and
`has("neo4j")` is false. **Both compilers emit the initializer** — `.init_array`
is present in both object files — so this is archive-member selection, not
codegen. Fixed with an exported `register_neo4j()`, mirroring
`db_sql_ext`'s existing `register_postgresql`. `[[gnu::used]]` would NOT have
fixed it: it stops the compiler discarding the object, and the object was never
the problem.

**sensen's pointer is deliberately NOT bumped.** The same libc++ fix is upstream
(`a76d589b`), but sensen master has moved **40 commits** of unrelated GPU/RL work
since this tree's pin, and dragging those in alongside a toolchain change makes
any failure unattributable. It is also not load-bearing here — the clang 23
engine linked at `rc=0` before that fix.

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
