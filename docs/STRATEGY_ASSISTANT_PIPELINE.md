# Strategy assistant: training-to-serving pipeline

`@author Olumuyiwa Oluwasanmi`

This is the end-to-end chain for `calculator.assistant.StrategyAssistant`'s
fine-tuned Qwen3-0.6B: how the training data is built, how the model is
trained, how it becomes the file the backend actually loads, and the serving
constraints that made it work in production. Each of these was learned by
breaking something once; this page exists so the next person does not have
to re-learn them the same way. The load-bearing ones are also stated as
comments next to the code they constrain (`agent/train/train.py`,
`agent/dataset/build_dataset.py`, `backend/src/modules/assistant_service.cpp`)
-- this page is the connected version of those same facts, not a separate
source of truth.

See CLAUDE.md's "Strategy assistant" section for the short summary; this
page is where "read the whole story" leads from there.

## 1. Dataset

`agent/dataset/build_dataset.py` generates a ShareGPT-shaped JSONL set from
`agent/dataset/strategies.json` -- the SAME catalogue
`backend/src/modules/strategy_catalogue.cppm` is generated from, so the
model's label space and the backend's validator cannot drift apart. Seven
generators cover the job (extraction, clarification, follow-up
modification, refusal, chitchat, unknown-strategy, unsupported-futures-root),
mixed at weights tuned so the dominant case (extraction, ~50%) doesn't crowd
out the behaviours that keep the model from treating every utterance as one.

Two things about this file are load-bearing beyond its own docstrings:

- **The system prompt** (`SYSTEM` constant) must stay byte-identical to
  `assistant_service.cpp`'s `kSystemPrompt`. See section 6.
- **`make_clarification()`'s four-turn shape** (user request, assistant
  question, user reply, assistant params) is matched exactly by
  `assistant_service.cpp`'s `build_prompt()`. The two must change together;
  see section 6.

Run it with `python3 build_dataset.py --out data/ --n 30000` from
`agent/dataset/`.

## 2. Training

`agent/train/train.py --method qlora`. Reused, not redesigned, across
retrains -- see the file's own top-of-module comment for the full rationale.
Summary:

| | value |
| --- | --- |
| method | QLoRA: 4-bit frozen base (`load_in_4bit`) + LoRA adapters trained in bf16 |
| rank / alpha | 16 / 16, on all seven projections (`q/k/v/o_proj`, `gate/up/down_proj`) |
| learning rate | 2e-4 (LoRA's standard rate; ~4x the full-fine-tune rate, because zero-initialized adapters must move further per step than an already-converged base) |
| optimizer | `adamw_8bit` |
| steps / wall clock | ~2056 steps, ~22 minutes, on the training host's smaller GPU |
| peak GPU memory | ~1.97 GB |

**Why QLoRA and not a full fine-tune.** Both were run to completion on the
same clean data and evaluated the same way:

| method | params exact-match |
| --- | --- |
| `--method qlora` (rank 16, all 7 projections) | **95.0%** |
| `--method full` (int8 QAT, all 398M params) | 49.8% |

At 0.6B, updating every parameter damaged the pretrained representations
faster than it learned a five-field JSON extraction task. QLoRA's frozen
base plus a small adapter leaves the pretrained weights alone and only moves
what has to change. **This is the bar for any future run**: below 95.0%
params exact-match on `agent/dataset/data/val.jsonl` is a regression
regardless of what else improved. Per-field baseline on the same set: symbol
98.7%, asset_class 99.0%, strategy 97.0%, expiration_days 100%, quantity
100%.

**Budget.** The whole chain -- train, merge, quantize -- should complete in
well under 30 minutes. Adding dataset rows is expected and fine; a run that
grows to take materially longer means something about the recipe changed,
not just the data volume, and is worth explaining rather than shipping
quietly.

## 3. Merge and export

`train.py` saves the LoRA adapter on its own first (a few MB, always
succeeds), then folds the adapter into the frozen base to produce a bf16
merged checkpoint (`save_pretrained_merged`, `save_method="merged_16bit"`).
That merged directory is an **intermediate artifact** -- convenient input
for llama.cpp's converter, not what ships and not what the accuracy bar
above is measured against once a quantized serving path exists (see section
4).

GGUF conversion (not automated by `train.py`; run by hand or by a follow-up
script):

```
python3 convert_hf_to_gguf.py <merged_dir> --outfile model-f16.gguf --outtype f16
llama-quantize model-f16.gguf model-Q8_0.gguf Q8_0
```

Output: a Q8_0 GGUF, ~639 MB, 310 tensors. The precision drop from bf16
(export) to Q8_0 (serve) is deliberate, not a contradiction with training at
4-bit: three different precisions serve three different purposes --
4-bit frozen base at TRAIN time (cheap, and never updated so its precision
doesn't bound final quality), bf16 adapters merged at EXPORT time (full
precision for the part that actually learned something), Q8_0 at SERVE time
(the CPU deployment target's actual runtime format).

## 4. Distribution

The GGUF lives in a **private HuggingFace repo** with its checksum pinned.
It is fetched at **Docker build time**, never through `railway up` -- that
CLI enforces an upload deadline that a 62 MB payload already failed, and the
GGUF here is roughly ten times that. `MODEL_URL`, `MODEL_SHA256` and
`MODEL_TOKEN` are Railway build variables, never committed.

**The trap in the build-time fetch**: `backend/Dockerfile`'s `model` stage
takes `MODEL_TOKEN` as a build ARG, not a `--mount=type=secret`. Railway's
Metal builder accepts only `type=cache` mounts and rejects the ENTIRE
Dockerfile if a `type=secret` mount appears anywhere in it -- and the
failure is silent: the build never starts, the previous container keeps
serving traffic, and `railway up` still exits 0. Five consecutive
deployments failed exactly this way before anyone looked at the deployment
list rather than the exit code. A local `docker build` will not catch it
either, because local BuildKit supports secret mounts fine; the failure is
specific to Railway's builder. Passing the token as a plain ARG is safe here
specifically because the fetch happens in its own `model` stage and the
runtime stage only does `COPY --from=model` -- the ARG's value never reaches
the layers of the image that actually get published, so it doesn't end up
in image history the way a `COPY` of a secret file would.

A checksum mismatch fails the build outright (never "falls back to no
model" -- a truncated or substituted GGUF loads fine and then generates
fluent, confident, wrong text, which is worse than a red build). An EMPTY
`MODEL_URL` is a deliberately supported no-op build (the calculator and the
general finance surface don't need the assistant; failing the whole image
over an optional feature would take down working functionality for an
unrelated reason).

## 5. Serving

`backend/src/modules/assistant_service.cpp`, production default backend
`sensen` (an upstream `llama.cpp` backend is also selectable; see the file's
own `InferenceBackend` comments for why both exist). Currently serves Q8_0
weights with a **q8 KV cache** (per-channel affine, f16 scale/minimum) and
`ASSISTANT_CONTEXT_TOKENS=4096`.

Six constraints here are load-bearing and each cost a debugging cycle:

1. **The exact training system prompt is mandatory.** `kSystemPrompt` in
   `assistant_service.cpp` must be byte-identical to `build_dataset.py`'s
   `SYSTEM` constant. Without it, the model reverts to stock Qwen3 --
   `<think>` blocks, no `<params>` block, ever. Changing so much as a comma
   silently converts a working extraction model into an uninstructed base
   model, and nothing downstream (parsing, validation, tests) tells you why.
2. **Qwen3 emits a `<think>` block on every response, including correct
   ones.** The block being *empty* is the signal the system prompt took.
   Treating its mere presence as failure rejects every valid answer.
3. **`n_gpu_layers` must be 0 on this CPU build.** A default-constructed
   `GenerationConfig` sets `compute_backend = AUTO`, which sensen counted as
   a GPU request; that enables `on_device_sampling`, a contract only the CUDA
   decode path honours, and the CPU path then casts a raw float logit to a
   token id -- 262 bytes of punctuation noise, byte-identical across runs,
   with only the first token correct. Fixed upstream in sensen and pinned
   independently here regardless.
4. **`generate()` is not safe to call concurrently.** sensen's
   `FeedForwardNetwork` keeps per-call scratch as `mutable` MEMBERS, not
   `thread_local` -- two threads decoding different requests would
   interleave writes into the same buffer and silently corrupt each other's
   hidden state (a wrong-answer race, not a crash). Concurrency instead
   comes from the iteration-level scheduler: one owner thread, one fused
   `forwardBatch` per step, batching every in-flight sequence.
5. **The four-turn clarification shape is matched exactly.**
   `build_dataset.py`'s `make_clarification()` produces user-request,
   assistant-question, user-reply, assistant-params; `build_prompt()` in
   `assistant_service.cpp` must reconstruct that same shape (with the
   trader's own `utterance` as the FIRST user turn and `prior_clarification`
   as a LATER user turn, never put in the assistant's mouth) or the model
   sees a shape it never trained on and produces a degraded near-miss
   instead of the trained answer. This broke once in production this way.
6. **Evaluate what ships.** The 95.0% bar in section 2 must be re-measured
   on the Q8_0 GGUF with the q8 KV cache through the production inference
   path -- not on the bf16 merged weights from section 3, which are an
   intermediate artifact no trader's request is ever actually decoded by.

## 6. Known limits of the trained model

As of the retrain documented alongside this page: futures roots were
broadened from `{ES, NQ}` to `{ES, NQ, CL, GC, ZB}` (the data provider now
returns a term structure for CL, GC and ZB too), and the model was taught to
ask a futures-or-options question on `ES`/`CL` -- both a futures root AND a
live equity (Eversource Energy, Colgate-Palmolive) -- rather than guess.
`RTY`, `YM`, `NG`, `SI`, `ZN` still have no data behind them and remain
taught as refusals naming what IS available.

`crack_321` (3-2-1 crack spread on CL) remains gated out of the UI on
purpose: it does not exist in `agent/dataset/strategies.json` or
`strategy_catalogue.cppm`, so broadening the CL *root* must never be read as
teaching the model that *strategy* -- the calculator would refuse to price
it even if the model emitted the id confidently.

Update this section with whatever the next retrain's own augmentation
changes; it is meant to be edited, not archived.
