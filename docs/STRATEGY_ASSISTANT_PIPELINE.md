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

### 2b. Model of record — what production serves today

The exact provenance of the GGUF currently pinned in `MODEL_SHA256`. Recorded
because "which model is this?" was answered three times from memory during the
2026-08-03 session and was wrong every time.

| | value |
| --- | --- |
| GGUF | `param-agent-qlora-v2-Q8_0.gguf`, 639,447,616 bytes |
| sha256 | `eab97cf531b0c3746e366afafaaf74f90bec2d9263f269cd66018605223d80ac` |
| trained | 2026-08-03, 11:38:04 → 12:03:22 UTC (23 m 27 s wall) |
| quantized | 2026-08-03, 15:20:56 UTC |
| base | `unsloth/Qwen3-0.6B`, `load_in_4bit=True` |
| epochs | **4** (`run_qlora.sh` passes `--epochs 4`) |
| steps | 2044 |
| batching | per-device 8 × grad-accum 4 (effective 32), `max_seq_length` 1024 |
| seed | 3407 |
| losses | `train_loss` 0.1741, `eval_loss` 0.1289 |
| data | `agent/dataset/data` — 28,500 train / 1,500 val |
| defect holdout | **16/16** (RPC layer, single verified engine, live market data) |

Reproduce with `/scratch/agents/run_qlora.sh` on the training host, then
`/scratch/agents/convert_to_gguf.sh <merged_dir> <out_dir>` (section 3).

**`--epochs 4` is the recipe; the script's default is 2.** `train.py` declares
`--argument epochs default=2.0`, and `run_qlora.sh` overrides it. Reading the
default as "the recipe" is what motivated a 2-epoch retrain on 2026-08-03 to
"correct an overfit"; it scored 5/16 against this model's 16/16 and was
discarded. Halving the epochs made it materially worse.

**This model was nearly thrown away.** It was benchmarked at 7/16 with
`llama-cli` — an engine that never serves a request — declared a regression, and
left unused on the training host while production served a different, worse file
(`91d4ea5d…`, 6/16). Measured through the real sensen serving path it is 13/16
raw, and 16/16 once three engine defects are fixed. Always score a candidate the
way `docs/guides/ASSISTANT_EVALUATION.md` describes, and always compare the
candidate's `sha256sum` against `MODEL_SHA256` before calling anything "the
deployed model".

## 3. Merge and export

`train.py` saves the LoRA adapter on its own first (a few MB, always
succeeds), then folds the adapter into the frozen base to produce a bf16
merged checkpoint (`save_pretrained_merged`, `save_method="merged_16bit"`).
That merged directory is an **intermediate artifact** -- the converter's
input, not what ships and not what the accuracy bar above is measured
against once a quantized serving path exists (see section 4).

GGUF conversion uses **sensen's own converter**, which is the standard here
for the same reason sensen is the standard for serving:

```bash
ninja -C backend/sensen/build convert_safetensors_to_gguf validate_gguf   # once
scripts/convert_to_gguf.sh <merged_dir> <out.gguf> q8_0
```

`sensen::convert_safetensors_to_gguf` (`backend/sensen/src/model_converter.cppm`)
writes Q8_0 **directly** from the merged safetensors. It replaced a llama.cpp
two-step on 2026-08-03:

| | llama.cpp | sensen |
| --- | --- | --- |
| steps | `convert_hf_to_gguf.py --outtype f16`, then `llama-quantize ... Q8_0` | one call |
| intermediate | a 1.2 GB f16 GGUF | none |
| wall clock | minutes | **0.765 s** |
| holdout | 16/16 | **16/16** |

The two outputs are **not** byte-identical -- ~480 bytes of metadata differ,
the weights do not -- so comparing checksums across the two writers proves
nothing. Compare behaviour, per `docs/guides/ASSISTANT_EVALUATION.md`. The
sensen path IS deterministic: repeated runs of the same merged checkpoint
reproduce the same sha256.

`scripts/convert_to_gguf.sh` refuses a `model_type` the converter does not
support (qwen3/qwen2 only) rather than emitting a GGUF that loads and then
generates confident nonsense, and runs `validate_gguf` on the result.

llama.cpp remains vendored, for two things this does not replace: the parity
probes under `backend/src/*_probe.cpp`, which need an INDEPENDENT
implementation to check sensen against (a reference sharing sensen's code
would prove nothing), and the opt-in `ASSISTANT_BACKEND=llamacpp` second
backend. Neither is a conversion or serving default.

Output: a Q8_0 GGUF, ~639 MB, 310 tensors. The precision drop from bf16
(export) to Q8_0 (serve) is deliberate, not a contradiction with training at
4-bit: three different precisions serve three different purposes --
4-bit frozen base at TRAIN time (cheap, and never updated so its precision
doesn't bound final quality), bf16 adapters merged at EXPORT time (full
precision for the part that actually learned something), Q8_0 at SERVE time
(the CPU deployment target's actual runtime format).

## 4. Distribution

> **SUPERSEDED, 2026-08-05 — the private HuggingFace repo named below was
> DELETED**, at the owner's instruction, because the weights are proprietary
> trade secrets and do not belong on a third-party registry, private or not. No
> model URL is pinned and **no model is in the deployed container**; the
> assistant answers `MODEL_UNAVAILABLE` and every other service is unaffected,
> which is the supported empty-`MODEL_URL` build this section already describes.
> A replacement hosting mechanism is being designed and is deliberately not
> described here — do not infer one from what follows, and do not re-upload to a
> registry to "restore" the fetch. `backend/Dockerfile`'s `backend/models/`
> staging path is a LOCAL-BUILD stopgap, not a Railway answer. Everything below
> about the MECHANICS of a build-time fetch — the checksum gate, the ARG-vs-secret
> trap, why `railway up` cannot carry the file — remains true and is what any
> replacement still has to satisfy.

The GGUF lived in a **private HuggingFace repo** with its checksum pinned.
It is fetched at **Docker build time**, never through `railway up` -- that
CLI enforces an upload deadline that a 62 MB payload already failed, and the
GGUF here is roughly ten times that. `MODEL_URL`, `MODEL_SHA256` and
`MODEL_TOKEN` are Railway build variables, never committed.

The same `model` stage now fetches a SECOND model on the same terms -- the
mortgage assistant's GGUF, under `MORTGAGE_MODEL_URL` /
`MORTGAGE_MODEL_SHA256` / `MORTGAGE_MODEL_PATH`. Everything in this section
applies to it unchanged; its own publish-and-pin procedure, checksum and token
scoping are in `MORTGAGE_MODEL_DISTRIBUTION.md`. Nothing else on this page is
about that model -- "the model" here always means this one.

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

### Swapping the served model

`MODEL_URL` and `MODEL_SHA256` must move together -- changing the URL alone
fails the build on the checksum, and changing the checksum alone fetches the old
file and fails the same way. Both are Railway variables, so this needs no code
change and no commit.

```bash
# 1. Publish. HF_TOKEN (config/.env) needs the `write` role.
python -c "
from huggingface_hub import HfApi
HfApi(token='<HF_TOKEN>').upload_file(
    path_or_fileobj='<candidate>.gguf',
    path_in_repo='<name>.gguf',
    repo_id='olumuyiwaoluwasanmi/options-param-agent-qwen3-0.6b',
    repo_type='model')"

# 2. Prove the bytes survived the round trip. The file MEASURED and the file
#    SERVED must be provably identical -- re-download and re-checksum, do not
#    assume the upload was faithful.
curl -sSL -H "Authorization: Bearer <HF_TOKEN>" \
  https://huggingface.co/olumuyiwaoluwasanmi/options-param-agent-qwen3-0.6b/resolve/main/<name>.gguf \
  -o /tmp/verify.gguf && sha256sum /tmp/verify.gguf

# 3. Repoint and redeploy.
railway variables --service options-calculator-backend \
  --set 'MODEL_URL=https://huggingface.co/.../<name>.gguf' \
  --set 'MODEL_SHA256=<sha>' --skip-deploys
railway up --detach --service options-calculator-backend
```

Then verify in production over **gRPC-Web** with a real Pro licence -- native
gRPC does not survive the Railway ingress, and the assistant is Pro-gated, so an
unauthenticated probe returns `grpc-status 7` and looks like a broken model.
`scripts/mint_pro_gate_creds.mjs` mints the licence; it rides `x-api-key`.

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
