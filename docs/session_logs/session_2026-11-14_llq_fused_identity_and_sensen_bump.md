# Session log — 2026-11-14 — LLQ fused identity, the Z3 removal, and a four-repository sensen bump

@author Olumuyiwa Oluwasanmi

## What this session changed

1. **A latent serving-correctness bug in the LLQ fused bf16 tier**, found behind two
   vacuous gates and fixed. Full account in `docs/ENGINEERING_NOTES.md` under
   "LLQ weight store: serving a model from its compressed image".
2. **sensen moved `37a522c5d` -> `2c460a106`** in all four repositories that vendor it, and
   the Z3 removal upstream was absorbed in OFC and SGEE.
3. **The Stripe outage was diagnosed and fixed**: both products can take subscribers again.
4. Tooling so the next credential rotation is one command per store.

## 1. The LLQ fused bf16 tier

The headline is in the engineering notes. What belongs here is the ORDER the evidence
arrived in, because three of the four conclusions along the way were wrong and each was
corrected by a measurement rather than by more reading.

- "The fused kernel computes wrong numbers" — **wrong**. Every finite output was already
  bit-identical.
- "Zero finite differences, so the kernel is fine" — **vacuous**, and my error. There were
  no finite outputs at all: the fixture injects inf/NaN deliberately, so 192 of 192 outputs
  were NaN and the assertion could not fail for a real reason.
- "The AVX-512 VBMI2 fused kernels are at fault" — **wrong**. Forcing `kFusedBf16 = false`
  (and confirming the translation unit recompiled) left the failure in place.
- "denseOrderChains in sensen has drifted" — **wrong file entirely**. There are two
  `llq_weight_store.cppm`. The failing code was `backend/src/modules/llq_weight_store.cppm`,
  this repository's own.

The fix is in OFC's module: `gemvSlice` stopped calling the codec's canonical kernel and now
sums with `dense_order::chains`, matching the dense kernel's order and rounding.

**For next time:** when a bit-identity gate fails, measure the SHAPE of the divergence before
reading any kernel. "How many differ, by how much, and are the values even finite" would have
reached the answer in one build instead of several hours.

## 2. sensen `37a522c5d` -> `2c460a106`, in four repositories

OptionsAndFuturesCalculator, mortgage-nest-egg, AkoEdeYorubaFunAwonEwe and
BigBrotherAnalytics all now pin the same commit.

Two defects in sensen blocked the embedded build and are fixed upstream on its master:

- agent/features/reason/CMakeLists.txt configured test_reason_feature unconditionally;
  sensen_add_feature only creates it under `if(BUILD_TESTS)`. Invisible standalone, fatal
  embedded. Every one of the other 23 features already guarded it.
- cmake/SensenSlim.cmake was missing `ce_core.cppm`, `moe_router.cppm` and
  `bf16_bidirectional_attention.{cppm,cpp}` plus its three device backends as `EXCLUDED`.

The closure check went 15 problems -> 1. The one that remains is sensen's and predates this
work: metal_decode sits in FOUNDATION while importing qwen38_prefix_cache from LLM, and
the comment above it still claims the metal family imports only metal_backend.

**sen is landing lanes in sensen continuously** — 82 commits arrived mid-merge and the first
`push ...:master` was rejected as non-fast-forward. Rebase and retry is the normal path there,
not an incident.

**`external/tbqwf` is NOT updated.** sensen records `96b31b76c093`; tbqwf's main is
`94ba03a4b802`. It stays deinitialised here (CLAUDE.md: never initialise sensen's submodules
recursively in this tree — it opens a flag gate and breaks the shared `std.pcm`), nothing in
OFC's closure compiles it, and bumping it is a sensen lane's job because only a sensen build
can gate it. Owner decision 2026-11-14: leave it for a lane.

## 3. The Z3 removal

sensen's `sen-no-z3` lane replaced `gp_ara::Z3Reasoner` with `gp_ara::SmtReasoner` over its
own native SMT stack. Nothing that ships links libz3: zero mentions in `gp_ara_agent.cppm`,
zero files under sensen's src/, zero in its root CMakeLists.

Three consumers, three different costs, decided entirely by what each coupled to:

| consumer | coupled to | cost |
|---|---|---|
| mortgage-nest-egg | the CONCEPT `ReasonerPolicy` | nothing — did not notice |
| OFC's reasoning test | `Z3Reasoner` via the `ContextType` alias | one identifier |
| SGEE's proof suite | Z3Reasoner::Z3Context, the concrete type | two identifiers and a header |

Both suites now link `sensen_reasoning_engine`, which is what sensen's own
`test_smt_reasoner_z3_parity` links. That also ended a duplicate build:
`gp_ara_interfaces.cppm` was being compiled FIVE times in one build directory.

SGEE's gate was the larger defect and would have outlived the rename — it was conditioned on
Z3_INCLUDE_DIR AND Z3_LIBRARY, which had stopped meaning "the reasoner is available". It is
now if(TARGET sensen_reasoning_engine), and the dead Z3 discovery probe (which printed
"✓ Z3 SMT Solver found for SGEE formal verification tests") is removed.

## 4. Stripe

OFC's checkout returned 502 Expired API Key provided: rk_live_.... **Nothing had been
rotated.** All three stores held one identical expired key, and that key was an `rk_live_` —
a CLI session key, which `stripe login` mints with a 90-day life. It aged out on schedule.

Fixed: Cloudflare `ofc-billing` and Railway `mfv-web` both now hold the new `sk_live_`
(fingerprint `8183fdd14f5a`), verified by `POST /checkout` returning 200 with a live
cs_live_ session. Prices are active, both webhook endpoints enabled, mfv's carrying its
`?env=live`, and the publishable key never changed so no rebuild was needed.

**Do not install a `stripe login` key as a service credential again** — that is the whole
cause. Dashboard-created restricted keys do not expire. OFC needs only Checkout Sessions:
write; mfv needs Customers write, Prices read, Subscriptions write.

Alpaca, separately: production is healthy and config/.env holds a MISMATCHED pair — a key
ID that does not match the secret beside it. Local dev only.

## Gates

- `scripts/sensen_module_closure.py --check`: 15 problems -> 1 (pre-existing, sensen's)
- the backend build directory configures rc=0, Clang 23.1.2, Brotli OFF
- `ninja build_tests`: 457 steps, 0 errors (405/405 targets, up from 403/405)
- `ctest`: **212/212 passing, 9 skipped**
- `test_llq_weight_store`: 118 checks 0 failures
- `test_sgee_automated_reasoning`: 23 checks 0 failures, discrimination probes included
- SGEE `replicated_queue_formal_verification_tests`: 6 properties proved

## Not done

- SGEE's sanitizer gate (its scripts/ directory, --all-transports) and sensen's lane
  precheck command
  were NOT run. These changes rest on a clean build and the suites above, which is real and
  is not those policies.
- `external/tbqwf` left one commit behind, by owner decision.
- The metal_decode layering violation in sensen is recorded, not fixed.
