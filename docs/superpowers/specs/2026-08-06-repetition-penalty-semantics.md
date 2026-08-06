# Repetition-penalty semantics: per-occurrence vs per-unique — is `fb2723cd` safe?

@author Olumuyiwa Oluwasanmi
Date: 2026-08-06
Scope: analysis only. Read-only over `backend/sensen`; no code changed, nothing committed or pushed.

## Question under review

Commit `fb2723cd` ("fix(llm): Penalise each repeated token once, not once per occurrence")
changed `Sampler::applyRepetitionPenalty` (`backend/sensen/src/llm_pipeline.cppm`) from
penalising every context *position* (so a token seen k times is divided by `penalty^k`)
to penalising each *distinct* token exactly once — the HuggingFace / llama.cpp convention.
The mirrored CUDA kernel `apply_rep_penalty_batch_kernel`
(`backend/sensen/src/cuda/cuda_llm_prefill.cu`) was changed the same way.

The concern: the per-occurrence compounding might have been DELIBERATE for
DiffusionGemma-style block-diffusion models, where the same token legitimately appears
many times across a denoised canvas and an occurrence-scaled penalty could be intended.
If so, the "fix" would be a silent regression for that model family.

## Verdict

**`fb2723cd` is safe to push as-is. No conditional design is needed; nothing should be
reverted.**

Three independent lines of evidence, each individually sufficient:

1. **The diffusion path never reaches this code.** The block-diffusion sampler family is
   a physically separate implementation (`dlm.cppm` EntropyBound/MaskedUnmask samplers,
   `cuda_diffusion_sampler.cu` on device) that contains **zero** repetition-penalty code
   and cannot even name `Sampler` (its imports don't include `sensen.llm_pipeline`).
2. **The per-occurrence loop predates all diffusion code by ~4.5 months** and was written
   as the simplest possible loop with no comment, no dedup, and no statement of intent —
   in the very first LLM-pipeline commit, long before block diffusion existed in the tree.
3. **Nothing in the tree depends on the compounding.** Every test that touches the penalty
   is either semantics-relative (compares two paths that share the same implementation) or
   was updated inside `fb2723cd` itself; both production calculator services pin
   `repetition_penalty = 1.0F` and are unaffected under either semantics.

## 1. Full caller inventory

### Host: `Sampler::applyRepetitionPenalty` (llm_pipeline.cppm:1166)

Called from exactly two places, both inside `Sampler`:

| Call site | Function | Classification |
| --- | --- | --- |
| `llm_pipeline.cppm:959` | `Sampler::sample` — penalty runs before the strategy switch | Autoregressive decode |
| `llm_pipeline.cppm:1010` | `Sampler::sampleGuided` — guided/grammar + logprobs path (#107) | Autoregressive decode |

`Sampler::sampleBatch` (llm_pipeline.cppm:1110) loops per-row into `sample()` — same path.

### Callers of `Sampler::sample` / `sampleGuided`

| Call site | Context | Classification |
| --- | --- | --- |
| `llm_pipeline.cppm:1994/1998` | `LLMPipeline` main generation loop (guided or plain) | AR decode |
| `llm_pipeline.cppm:2235` | Batch generation loop (`parallelFor` over agents) | AR decode |
| `llm_pipeline.cppm:2951` | `LLMPipeline::schedulerSample` (public scheduler seam) | AR decode |
| `server/serve_reactor.cppm:1428, 1644, 1672, 1949, 2000, 2366, 3528` | Reactor: first-token-after-prefill and per-step decode sampling | AR decode, all of them |

Every `schedulerSample` call in the reactor belongs to the causal (prefill/decode)
scheduling paths. Diffusion sequences route through `schedulerDiffusionStep` /
`schedulerDiffusionStepEbDevice` (serve_reactor.cppm:2522, 2581, 2716), which never call
`schedulerSample`.

### Device: `apply_rep_penalty_batch_kernel` via `sensen_cuda_rep_penalty_argmax_batch`

The kernel exists to reproduce the host function bit-identically for on-device greedy
sampling (its own doc comment, and `llama_model.cpp:3055-3082`: "the device computes
argmax(rep_penalty(softcap(raw))) bit-matching the host … exactly the slice
`Sampler::applyRepetitionPenalty` walks").

| Call site | Context | Classification |
| --- | --- | --- |
| `llama_model.cpp:3122` | `forwardBatchDecodeGreedy_cuda` — per-launch batched greedy decode | AR decode |
| `llama_model.cpp:4799` | CUDA-graph greedy decode body (captured) | AR decode |
| `cuda_backend.cppm:2086, 2410` | `DecodePipeline` (fluent `with_repetition_penalty`, llama_model.cpp:11483) — on-device sampling tail of single-token `forwardDecode_cuda_launch` | AR decode |

**Every caller of both the host function and the device kernel is autoregressive
decode. There is no non-AR caller anywhere in the tree.**

### Parent repo (`backend/src/`)

No direct `Sampler` calls. The two services that generate text —
`modules/assistant_service.cpp:660` and `modules/mortgage_assistant_service.cpp:586` —
both set `config.repetition_penalty = 1.0F` explicitly, with comments citing
`Sampler::sample`'s behaviour as the reason. Under 1.0 the penalty branch is skipped
entirely, so these callers are byte-identical under old and new semantics. Probe tools
(`assistant_throughput_probe.cpp`, `decode_golden_probe.cpp`) construct
`GenerationConfig` for the same AR path.

## 2. Does DiffusionGemma's path reach this code? — No, provably

The concern is moot, with call-graph evidence:

- Diffusion serving enters at `LLMPipeline::schedulerDiffusionStep` /
  `schedulerDiffusionStepEbDevice` (llm_pipeline.cppm:2646/2695), which call
  `forwardDiffusionBlock` / `forwardDiffusionBlockDeviceSample` /
  `forwardDiffusionBlockBatchedDeviceSample` on the model — **never**
  `impl_->sampler->…`.
- The host diffusion sampler is `dlm::EntropyBoundSampler` (and `MaskedUnmaskSampler`) in
  `src/dlm.cppm`. That file's imports are `std`, `sensen.diffusion_core`,
  `sensen.cpu_features`, `sensen.parallel` (dlm.cppm:78-83) — it cannot reference
  `Sampler` even by accident. `grep -in "repetition\|penal"` over `dlm.cppm`,
  `diffusion_core.cppm`, `trainable_backbone_model.cppm` and
  `cuda/cuda_diffusion_sampler.cu` finds **zero** repetition-penalty logic.
- The device diffusion sampler is `sensen_cuda_eb_sample`
  (`cuda_diffusion_sampler.cu`, "the GPU analog of
  `dlm::EntropyBoundSampler::step_once`") — an entropy-bound CDF sweep with a
  temperature schedule. No penalty of any kind.
- The one "repetition" hit in `dlm.cppm` (line 2496, `trim_canvas`) is a **post-hoc
  text-level repetition-loop trim heuristic** that operates on decoded token sequences
  after sampling. It reads tokens, not logits, and is unaffected by any penalty
  semantics.

So there is no code path by which a block-diffusion model's canvas ever passes through
`applyRepetitionPenalty` or its CUDA mirror.

## 3. Archaeology — what did the original author intend?

- `git log -S applyRepetitionPenalty` bottoms out at **`ad60b068`, 2026-01-29**,
  "feat(agents): Add coding agent framework, LLM pipeline, and Python bindings" — the
  commit that created the LLM pipeline itself.
- The original implementation (quoted from `ad60b068:src/llm_pipeline.cppm`):

  ```cpp
  for (std::size_t i = start; i < context.size(); ++i) {
      const std::uint32_t token = context[i];
      if (token < logits.size()) {
          if (logits[token] > 0) { logits[token] /= penalty; }
          else                   { logits[token] *= penalty; }
      }
  }
  ```

  No dedup, no comment, no doc string beyond "Apply repetition penalty". Nothing in the
  commit message or any document of that era mentions per-occurrence semantics, let alone
  justifies them.
- The diffusion stack did not exist until **2026-06-11**: `ef79f5d8` (F21 diffusion kit)
  and `441a03e1` (F29 dlm/MDM) — ~4.5 months after the penalty was written.
  DiffusionGemma serving in `trainable_backbone_model.cppm` came later still
  (`0d5c51c2`).
- Conclusion: the per-occurrence loop **cannot** have been written for block-diffusion
  models, and there is no evidence anywhere of it being intentional for anything. It
  reads as the natural first-pass implementation that simply omitted the dedup the
  HF/llama.cpp references perform. The sign convention (divide positive / multiply
  negative), which *is* the deliberate part, was preserved by `fb2723cd`.

## 4. Is per-occurrence behaviour depended upon anywhere?

Checked every test, doc, and tuned default that references the penalty:

| Artifact | Depends on compounding? |
| --- | --- |
| `tests/test_repetition_penalty.cpp` (new in `fb2723cd`) | Encodes the NEW semantics; 62/62 pass locally (re-run during this analysis). Discriminating: reintroducing the old loop fails 28 checks. |
| `tests/test_gpu_argmax_softcap.cpp` | Host reference updated inside the same commit to match. |
| `tests/test_gpu_chunked_prefill_reppenalty.cpp` | Semantics-relative: both legs (serial vs chunked) sample through the same `schedulerSample`, so it tests context-seeding equivalence, not the exponent. Unaffected. |
| `tests/test_gpu_batched_decode.cpp` (Test 7) | Semantics-relative: compares CUDA-graph vs per-launch device paths, which share the one kernel (changed together). Tests window/stride bugs (rep_window=0 clamp), not the exponent. Unaffected. |
| `tests/test_batched_prefill_uniform_gate.cpp` | Only checks that penalty *config values* are uniform across a batch for admission gating. Unaffected. |
| `docs/api/api_reference.md` | Documents the default (1.1) and a loose description; no per-occurrence contract stated. |
| Tuned defaults `repetition_penalty = 1.1F`, `repetition_window = 64` (`llm_interfaces.cppm:104`) | The 1.1/64 pairing is the ecosystem convention, which was defined *with* dedup semantics in HF/llama.cpp. Under the old code the effective penalty at 1.1 was silently `1.1^k` — the measured 0/90 defect. The tuned default becomes MORE faithful under the fix, not less. `AgentSession`'s per-agent field defaults to 1.0 (`llm_interfaces.cppm:531`). |
| Calculator services (parent repo) | Pin 1.0 explicitly — no-op under both semantics. |

No test, benchmark, doc, or default encodes or relies on `penalty^k` compounding.

## 5. If some caller ever DID want occurrence-scaled penalisation

Not needed today, but recorded for the future: the right mechanism already exists.
`Sampler::applyFrequencyPenalty` (llm_pipeline.cppm, directly below the repetition
penalty) is the count-proportional penalty — it counts occurrences and subtracts
`count * frequency_penalty` linearly, which is the principled "scales with how often the
token appeared" behaviour (the OpenAI-style convention). A hypothetical diffusion-side
need for occurrence-aware suppression should be expressed through `frequency_penalty`
(or a diffusion-native mechanism in `dlm.cppm`), **not** by restoring exponential
compounding inside `repetition_penalty` or adding a config switch. A
per-occurrence-vs-per-unique mode flag was considered and rejected: it would carry a
defect as a selectable behaviour, with no caller to select it.

## 6. What could NOT be determined here — stated plainly

1. **The CUDA kernel change is compiled and run by nobody on this machine**
   (`ENABLE_CUDA=OFF`, no nvcc — the commit message says the same). Host–device
   bit-match is preserved *by construction* (both sides dedup identically) and is
   covered by `test_gpu_batched_decode` Test 7 / `test_gpu_argmax_softcap` on a CUDA
   host, but those gates have not been executed against `fb2723cd` on real hardware.
   They should be run on the next CUDA build before relying on the device greedy path.
2. **Out-of-tree consumers.** `Sampler` is an exported class, so an external project
   could in principle call `applyRepetitionPenalty` directly and observe the change. No
   such consumer exists in this repository or the parent calculator; none is known.
3. **Behavioural note for AR callers with penalty > 1.0** (none in production today):
   for genuinely repetitive output, dedup makes the effective suppression *weaker* than
   before (once instead of k times). Any downstream user who empirically tuned a small
   penalty value against the compounding behaviour would feel that change. There is no
   evidence of such tuning anywhere in either repo — the only non-1.0 values in the tree
   are test fixtures and the inherited 1.1 default.

## Evidence ledger

- Callers: `grep -rn "applyRepetitionPenalty\|sampler->sample\|sampleGuided\|rep_penalty_argmax"` over `src/`, `server/`, and parent `backend/src/` (2026-08-06).
- Diffusion isolation: `dlm.cppm` import list (lines 78-83); zero penalty hits in `dlm.cppm` / `diffusion_core.cppm` / `trainable_backbone_model.cppm` / `cuda_diffusion_sampler.cu`; reactor diffusion entry points at `serve_reactor.cppm:2522/2581/2716`.
- Archaeology: `git log -S applyRepetitionPenalty` → `ad60b068` (2026-01-29); diffusion modules added `ef79f5d8`/`441a03e1` (2026-06-11).
- Gate re-run during this analysis: `build/bin/test_repetition_penalty` → 62 passed, 0 failed.

## Addendum (2026-08-06, at push time)

The commit analysed above was rebased onto `2f68aba4` before publication and is now
`fb2723cd` on both sensen remotes; the SHA references in this document have been
updated to the published one. The rebase was content-identical (`git show` of the
pre- and post-rebase commits diffs empty) and touched a disjoint file set from
`2f68aba4`, so nothing in this analysis is affected.

The conclusion stands unchanged: the fix is safe, the DiffusionGemma concern is moot
(`dlm.cppm` cannot name `Sampler`, and the diffusion stack postdates the penalty by
four months), and the CUDA half remains unexecuted on this `ENABLE_CUDA=OFF` host.
