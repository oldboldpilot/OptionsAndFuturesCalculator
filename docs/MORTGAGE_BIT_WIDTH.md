# Mortgage assistant at 4, 8 and 16 bits: accuracy against throughput (no LLQ)

@author Olumuyiwa Oluwasanmi

Measured 2026-09-30 on `oluwasanmi-fedora-server` (AMD Ryzen 9 9955HX, 16 cores /
32 threads, no GPU, `n_gpu_layers = 0`), through the real
`mortgage.assistant.MortgageAssistant/ParseOperation` RPC on **sensen**.
The LLQ rows are measured below; §A records what each instrument can and cannot see.

## The table

| bits | LLQ | raw_exact | served_exact | decode tok/s | prefill ms | model sha256 |
|---|---|---|---|---|---|---|
| 4 (Q4_0, **double-quantised**) | no | **241/560 = 43.0%** | 291/560 = 52.0% | **39.5** (paired; 41.6 / 39.9 / 37.0) | **~1,040** | `ee23f62fe7c0b037c0da918ab1b8275b27be376ee4e230645e5fcc85bc19041e` |
| 8 (Q8_0, deployed v20) | no | **414/560 = 73.9%** | 431/560 = 77.0% | **62.2** (paired; 63.6 / 63.2 / 59.7) | **~165** | `e885c57b0ca139476fd86c34694c9a130ceaba10335211ab4c15cfbf9dfa6024` |
| 16 (F16, **upcast from Q8_0**) | no | **76/95 = 80.0%** on the first 100 rows only (Q8_0 on the same rows: 73/95) | 73/95 (Q8_0: 72/95) | **4.7** (paired; 4.79 / 4.87 / 4.50) | **~11,900** | `ec7248c4708d672be1123923aacc7a8f3370d68b13069262526f1686eaec8272` |
| 4 | yes | **REFUSED at boot** -- the store adopts 0 of 196 weights (`196 left dense (other qtype)`) and the engine logs `LLQ was requested but no Q8_0 weight was adopted` and makes the assistant unavailable. Not a gap: it is what stops a run being quoted as an LLQ number while the dense path executed. |
| 8 | yes, `llq` | **414/560 = 73.9%, IDENTICAL to dense** | **431/560, identical** | **44.59** | ~same | same file, `e885c57b...fa6024` |
| 8 | yes, `llq-fused` | **415/560 = 74.1%** (paired vs dense: +8 / -7, net **+1**, McNemar **p = 1.0**) | **431/560, same total** | **47.82** | ~same | same file |
| 16 | yes | not served yet -- the fused bf16 tier is being wired | — | — | — | `v21-bf16.gguf`, 1,198,182,496 B |

**AT 8 BITS LLQ IS A NO-OP IN ALL THREE DIMENSIONS, and that is the result
rather than a disappointment.** Bytes +0.07%, decode 0.91x materialising and
0.976x fused, accuracy indistinguishable. The cause is one measurable property:
GGUF Q8_0 refits a scale every 32 weights, so within a block the codes already
fill their range -- the byte-minimal base comes out EQUAL to the source width on
all 197 tensors, the CSR residual is zero bytes and the outlier fraction is
0.0000%. With no bytes saved there is nothing for a fused kernel to read faster.

sensen's own `docs/technical/LLQ_COMPRESSION_EVIDENCE.md` (`a8e272cb`) states
this as a general property from the other direction, measured on Qwen3.8-27B at
group-64: *"A scale per 64 weights spreads each group across its full 4-bit
range, so no code falls outside B = 4, B = S, there is no residual, and LLQ
stores exactly the dense codes."* Its §6 decision table tells a group-scaled
caller to keep the dense codes. We are that caller at 8 bits, and these rows are
the same conclusion reached independently at group-32.

**The 15 rows that move under `llq-fused` move in BOTH directions, which is the
signature to expect.** `LlqGemv::runInt8Exact` computes an exact integer total
per 32-weight block, where the dense kernel keeps an 8-lane FMA accumulator and
reduces once; the integer arithmetic is exact and only the float reduction ORDER
differs, so greedy decoding flips near-ties either way. 8 gained and 7 lost at
p = 1.0 is that, and it is NOT a licence to treat the tier as identical -- a
future model could sit closer to more ties.

**THE TWO 8-BIT LLQ ROWS ARE DIFFERENT TIERS AND MUST NOT BE MERGED.** `llq`
materialises the image back into a Q8_0 tile and runs sensen's EXISTING kernel,
so its output is byte-identical to dense by construction -- and it does strictly
more work for the same bytes read, which is why it is the slowest arm.
`llq-fused` runs `LlqGemv::runInt8Exact` on the packed image: the integer dot
product is exact, but it replaces an 8-lane FMA accumulator with per-block
integer totals, so the float reduction ORDER differs and near-ties flip.
Measured: token sha `a1168d29480d` for dense and `llq`, `fbaa459f8bf3` for
`llq-fused`. An accuracy figure from one tier says nothing about the other.

Other harness cells, all measured: 4-bit `asked_ok` 66/86, `answered_ok` 50/68,
0 errors; 8-bit `asked_ok` 64/86, `answered_ok` 68/68, 0 errors (reproduces the
owner's 414 / 431 / 64 / 68 exactly). The 16-bit arm's `asked_ok`/`answered_ok`
on its 100-row subset are 12/17 and 9/9 (Q8_0 same rows: 13/17, 9/9).

## THE 16-BIT ACCURACY IS NOT IDENTICAL TO 8-BIT

The prediction was identity, because dequantising Q8_0 to F16 adds no
information. It does not hold. On the same first 100 rows F16 is 76/95 raw and
Q8_0 is 73/95: F16 fixes 4 rows and breaks 1, and 95 of the 125 raw model
outputs are byte-identical. `asked_ok` also moved 13 -> 12.

This is not a pipeline fault, and the reason is measurable in the tree: the
weights are the same numbers, but the two kernels are different arithmetic. The
Q8_0 path quantises the ACTIVATIONS to int8 before the dot product, and the F16
path does not, so the logits differ slightly even with identical weights and
greedy decoding flips the near-ties. Read the +3 as "removing activation
quantisation is worth a few rows", not as "16 bits is more accurate than 8":
n = 95 paired rows, 4 gained against 1 lost, is not significant, and it is a
subset. **A full-holdout F16 run was started and aborted** (see below), so no
figure exists for the full 560.

## Why the F16 accuracy is a 100-row subset

F16 decodes at 4.7 tok/s with a median 11.6 s prefill (paired table below). A
full 734-generation pass is about 8 hours on this host, so it was run on the
first 100 holdout rows (`--n 100`, 125 generations, ~55 min) against the Q8_0
engine on the same rows. The aborted full run is not in the table.

## Throughput, measured properly: paired and alternating

The per-arm full-run timing figures are NOT comparable, because another agent
was running CPU benchmarks (`llq_throughput_*`, `bench_*`) and compiling on this
host for most of the wall time. Load average over the Q4_0 full run swung
5 to 29 and its decode rate fell from 41 tok/s in the first 50 generations to
21 tok/s by the 250th; its whole-run aggregate (29.7 tok/s) and median (37.2)
diverged, which is the tell. They are kept as files but not quoted.

The table uses instead a paired, alternating benchmark: Q8_0, Q4_0, F16 in turn,
three rounds, a fresh engine per arm, the same holdout rows for each arm
(`--n 25` for Q8_0 and Q4_0, `--n 8` for F16), 30 / 30 / 4 generations. Load
average during it was 5 to 9 (`tp_rounds.log`), so it is not an idle machine
either, but each round exposes all three arms to the same conditions.

| arm | round 1 | round 2 | round 3 | mean aggregate decode tok/s | median prefill ms |
|---|---|---|---|---|---|
| Q8_0 | 63.59 | 63.23 | 59.70 | 62.2 | 160-177 |
| Q4_0 | 41.64 | 39.93 | 36.97 | 39.5 | 1,005-1,098 |
| F16 | 4.79 | 4.87 | 4.50 | 4.7 | 11,750-12,320 |

**Fewer bits is SLOWER here, not faster.** Q4_0 decodes at 0.64x of Q8_0 and
prefills ~6x slower; F16 decodes at 0.08x and prefills ~70x slower. sensen's
Q8_0 kernel is the tuned one on this CPU, and the Q4_0 and F16 paths are not.
That is a statement about these kernels on this host, not about the formats: a
memory-bandwidth-bound decode should favour fewer bytes, and F16 reading 1.2 GB
per token at 4.7 tok/s is only ~5.6 GB/s.

### THE 8-BIT BASELINE DISAGREES WITH THE 15.42 tok/s QUOTED FROM THE OWNER'S RUN

The same Q8_0 file, the same `timing:` lines and the same parser give **63.3
tok/s** aggregate over the 721 generations of the full run (median 64.1, prefill
median 167 ms, output tokens median 96 - which does match the 15.42 run's
96, so the workload is the same), and 59.7 to 63.6 across the paired rounds.
That is 4x the recorded 15.42. I did not reproduce 15.42 and do not know what
produced it. What differs from that run, by construction of the script, is that
mine uses `INFERENCE_QUEUE=local` (the ambient config points at the production
Postgres via `DATABASE_URL`, so `postgres` was deliberately not used), a private
engine copy, quotas disabled, and default thread settings (4 inference threads).
The LLQ column should be compared against the 62.2 above, not against 15.42,
unless whoever measures it runs under the conditions that gave 15.42.

## What is a real measurement and what is bounded

| figure | status |
|---|---|
| 8-bit raw 414, served 431, asked 64, answered 68 | **Real**; reproduces the owner's run exactly, in this script |
| 8-bit throughput 62.2 (paired) / 63.3 (full run) | **Real** for this script's conditions; **disagrees with 15.42**, unexplained |
| 4-bit raw 241, served 291 (full 560 holdout) | **Real measurement of a DOUBLE quantisation, a FLOOR.** bf16 -> Q8_0 -> Q4_0. A proper bf16 -> Q4 would score higher. The QLoRA adapter is not on this machine, so it cannot be produced here |
| 4-bit additionally pessimistic | `llama_model_quantize` is called by `gguf_requantize_probe` with `pure = true`, so EVERY tensor including the tied `token_embd`/`lm_head` is Q4_0 (engine log: "196 Q4_0"). A mixed recipe that keeps the embedding higher would score better. The probe has no flag for it and changing it needs a build |
| 4-bit throughput 39.5 (paired) | **Real** for Q4_0 on sensen; the full-run 29.7 is contaminated by host load and not quoted |
| 16-bit accuracy 76/95 | **Real but a 100-row subset**, and of an UPCAST: it carries no information Q8_0 lacks, so it measures kernel arithmetic, not a 16-bit model. No full-holdout figure exists |
| 16-bit throughput 4.7 (paired, 12 generations total) | **Real**, small sample (4 generations per round) but the round-to-round spread is 4.50 to 4.87 and every F16 generation in the aborted full run and the subset run also gave 4.7 to 4.8 |
| all LLQ rows | **Not measured here**: pending another agent |

## Caveats

1. **Deviation from repo policy.** `CLAUDE.md` says llama.cpp is a debugging and
   cross-checking tool only, and that `sensen::convert::requantizeGguf` is the
   conversion standard. The variants were produced with
   `backend/build/gguf_requantize_probe`, which calls llama.cpp's
   `llama_model_quantize`, because a sensen driver would have needed a build and
   `backend/build` was in use by another agent. It matters little for the F16
   upcast (mechanical) and most for 4-bit, where the quantisation recipe changes
   the answer. **All accuracy and throughput figures were measured on sensen**,
   never on llama.cpp.
2. The 4-bit weights are Q4_0 (llama.cpp ftype 2). No other 4-bit scheme (Q4_K,
   IQ4) was tried; sensen loaded Q4_0 and F16 without error.
3. Engine flags mirror production where they bear on the answer:
   `repetition_penalty` 1.0 and `n_gpu_layers` 0 are pinned in the service.
   Non-production settings: `INFERENCE_QUEUE=local`, `DATABASE_URL` empty,
   `PRO_GATE_MODE=off`, `QUOTA_POLICY` empty. The quota matters: with the ambient
   policy the anonymous compute budget refused ~95% of requests with
   `RESOURCE_EXHAUSTED` and the harness reported "RPC errors: 594", which is an
   artefact, not a model result.
4. A first Q4_0 run was killed by the tool's background time limit at 489 of 734
   generations and discarded; the figures come from a complete rerun.

## Harness, per run

- Holdout: `agent/dataset/data_mortgage/val.jsonl`, sha256 begins
  `1aa3ce94c344217e...`, 600 rows, 560 with gold params; disjoint from
  `train.jsonl` (11,400 rows), asserted by `--assert-disjoint-from` on every run.
- Engine: a private COPY of `backend/build/calculator_engine` (29,933,648 bytes,
  built Sep 29 16:10), started through `scripts/run_with_env.py`, on its own
  `ENGINE_GRPC_PORT` (50161-50164; never `PORT`). Each arm logs
  `Mortgage assistant model is LOADED`. Default assistant threads (4),
  `MORTGAGE_ASSISTANT_MAX_CONCURRENT` 4, sequential harness.
- **One-engine assertion, every run** (`<arm>.oneengine`): exactly one process
  running the private binary copy and exactly one listener on the port
  (`ss -ltn`), so `SO_REUSEPORT` cannot have split requests across models. The
  unrelated production-style engine on :50051 (v20) was left alone.
- Model files hashed directly (the `*_SHA256` variables enforce nothing on a
  locally staged file). Variants were written to
  `/home/muyiwa/llq-scratch/` (not in the repo, not committed):
  `v20-q4_0.gguf` 341,454,944 bytes, `v20-f16.gguf` 1,198,182,496 bytes. The
  v20 Q8_0 original was never touched.
- Throughput: `scripts/parse_engine_timing.py` over the engine's
  `[mortgage-assistant] timing: prefill=..ms decode=..ms tokens=N` lines;
  aggregate = sum tokens / sum decode seconds.
- Host load is recorded in `docs/evidence/bit-width-no-llq/load.log` and
  `tp_rounds.log`.

## Reproduce

```
backend/build/gguf_requantize_probe <v20-q8_0.gguf> out-f16.gguf 1   # F16
backend/build/gguf_requantize_probe <v20-q8_0.gguf> out-q4.gguf  2   # Q4_0
cp backend/build/calculator_engine /scratch/engine
scripts/measure_bit_width_arm.sh <label> <gguf> <port> /scratch/engine <outdir>
# EVAL_ARGS="--n 100" limits rows
python3 scripts/parse_engine_timing.py <outdir>/<label>.engine.log
```

Raw outputs: `docs/evidence/bit-width-no-llq/`. The per-row JSON (up to 0.7 MB
each) is not committed.
