# Session 2026-08-05 — a 27%→0% collapse caused by one config default, model hosting off Railway, and five harnesses that each lied

@author Olumuyiwa Oluwasanmi

The headline is small and expensive: **`GenerationConfig::repetition_penalty`
defaults to `1.1F`, sensen applies the penalty to the logits *before* the greedy
argmax, and `applyRepetitionPenalty` compounded it per occurrence rather than per
unique token.** Qwen3 tokenises numbers one digit per token, so in a JSON payload
the ASCII `'0'` token appears eight to twelve times inside a 64-token window and
gets divided by `1.1^k`. Greedy decode then genuinely preferred a *different*
token — including U+FF10, the fullwidth zero, which is a different token id and so
escaped the penalty entirely. Exact-match went **0/279** through the real RPC.
Pinning `repetition_penalty = 1.0F` took two independent A/B harnesses from
**0/90 → 25/90** and **0/120 → 33/120**.

Everything else in this log is either the elimination path that got there, or the
tooling that made it take as long as it did. The eliminations are recorded at the
same weight as the answer, because four of them were plausible enough to have been
"fixed" instead — and because one of them (the converter) *was* wrong, just not
causal.

Three things are recorded as unresolved or unverified and are flagged where they
sit: the **mirrored CUDA kernel fix is UNCOMPILED and UNTESTED**, `PRO_GATE_MODE`
is untouched, and the mortgage assistant's absolute accuracy after the fix
(25/90, 33/120) is a working model, not a shipped one.

---

## 1. The collapse: 27% raw decode, 0% through the service

### Symptom

The fine-tuned Qwen3-0.6B mortgage assistant scored **0/279 params exact-match**
through `mortgage.assistant.MortgageAssistant/ParseOperation` — the actual RPC,
the actual GGUF, the actual in-process sensen backend. Raw decode of the same
GGUF scored **8/30 (27%)**. A gap that large between "the model in a harness" and
"the model in the service" is normally a serving defect, and that is where the
session started looking.

The output was not degraded in the way a bad checkpoint degrades. It was
*structurally* correct JSON with corrupted characters wedged inside numeric
literals:

```
"term_months":18０
```

That trailing character is **U+FF10 FULLWIDTH DIGIT ZERO**, not an ASCII `0`.
U+2080 (SUBSCRIPT ZERO) appeared the same way. Alongside it: invalid JSON
(`"timing"]=`, unquoted keys) and fabricated trailing digits on otherwise correct
numbers. A model that had learned the schema well enough to emit
`"term_months":18` and then put a *fullwidth zero* on the end is not a model that
forgot how to count.

### Four hypotheses, eliminated by measurement

Each of these was live, each was cheap enough to test, and each is worth recording
because the elimination is reusable.

**1. KV-cache dtype.** The KV cache had recently moved to q8; a quantised cache
corrupting late-sequence tokens would look exactly like fabricated trailing
digits. Probed with `SENSEN_KV_DTYPE=fp32`. It scored **identically to q8 — at most
a one-row difference**. Not the cause, and the q8 cache is exonerated for this
class of defect.

**2. Vocabulary corruption.** If the GGUF's tokenizer table had been mangled by
conversion, a homoglyph substitution is precisely the symptom. Checked directly:
**tokens 15–24 are `'0'` through `'9'`**, PAD is correct, and **0 of 13 sampled
tokens differed** from the known-good options-assistant GGUF. The vocabulary is
intact — and this result is what later made the penalty explanation *provable*,
because it pins ASCII `'0'` to token id 15 and lets the bisect trace below be read
literally.

**3. System prompt.** This model, like the options assistant, requires its
training system prompt verbatim or it reverts to stock Qwen3. Rather than eyeball
it, the prompt was extracted programmatically from
`agent/dataset/data_mortgage/train.jsonl` row 0 and `cmp`'d byte-for-byte against
`kSystemPrompt` in the service. **322 chars, identical, no diff.** Eliminated.

**4. Converter version.** This one was a real finding that turned out not to be
the cause. The shipped GGUF had been converted with a **Jul-24 binary from a
different sensen checkout (`299cc4fc`)**, not the repo's own **Aug-3 build
(`4d4b4cbd`)**. That is exactly the wrong-artifact class of error this project has
been bitten by three times already, so it looked promising. Reconverting with the
correct binary produced a **byte-identical file — same sha256 `269efd32…`**.
Sloppy provenance, zero behavioural difference. Recorded rather than quietly
dropped, because "we were building with the wrong binary" is true and worth
fixing even when it is not the bug.

### The test that actually discriminated: one GGUF, two independent engines

With the model, the vocabulary, the prompt and the file all cleared, the remaining
suspects were sensen's decode and the service around it. The only way to separate
those is an **independent implementation** — which is the one legitimate role
llama.cpp has in this repo (per CLAUDE.md: debugging and cross-checking, never
serving, never conversion).

`backend/src/numeric_audit_probe.cpp` was generalized to do it: it now takes a
**system-prompt file** and an **utterance file** rather than hardcoded strings,
honours **`AUDIT_NCTX`**, reports **first divergence** between the two engines,
and **flags non-ASCII output** explicitly rather than letting a fullwidth digit
pass through a terminal looking like a normal one. It loads **one GGUF through
both llama.cpp and sensen** in the same process.

Over 30 utterances:

| engine | score | non-ASCII digits emitted |
| --- | --- | --- |
| llama.cpp | **8/30** | **0** |
| sensen | **8/30** | **0** |

**21/30 produced identical params objects.** llama.cpp independently hallucinated
`"ComputeRentalRccapRate"` and produced the one invalid-JSON block, which is a
useful control in its own right: the two engines fail in the *same places* and in
uncorrelated *ways*, which is what genuine model weakness looks like.

The verdict was unambiguous and it pointed away from where the search had been:
**neither engine's decode was at fault, and neither emitted a single non-ASCII
digit.** The 27%-vs-0% gap was not decode. It was configuration — something the
service set that the probe did not.

### Root cause

`sensen::Sampler::sample` (`backend/sensen/src/llm_pipeline.cppm`) applies
`repetition_penalty` to the logits **before** the greedy argmax. So "greedy" in
sensen means *argmax of penalised logits*, not argmax of raw logits.

`GenerationConfig` defaults the penalty to **1.1 over a 64-token window**
(`backend/sensen/src/llm_interfaces.cppm:104` and `:107` — verified in the tree:
`float repetition_penalty = 1.1F;` and `std::size_t repetition_window = 64;`).
The service never overrode it.

And `applyRepetitionPenalty` compounded **per occurrence**: a token seen *k* times
in the window was divided by `penalty^k`. **HuggingFace and llama.cpp both penalise
once per UNIQUE token.** That is the divergence — not the presence of the penalty,
its accumulation.

Qwen3 tokenises one digit per token. A mortgage params object is mostly numbers.
ASCII `'0'` lands in the 64-token window **8–12 times**. The bisect trace, which
reads directly against the vocabulary check from hypothesis 2 (`'0'` is token 15,
`'1'` is token 16):

```
step 24  raw=15 (logit 43.7148, seen 7x -> /1.100^7)  sampled=16 (raw logit 27.4741)
         43.71 / 1.1^7 = 22.4 < 27.47  ->  '0' becomes '1'
```

The model was right. The sampler overruled it, deterministically, on the most
common token in its own output.

**The homoglyph is the same mechanism, and it is the part that proves the
diagnosis.** U+FF10 is a **different token id** from ASCII `'0'`. It had not
occurred in the window, so it took no penalty at all, while ASCII `'0'` was being
divided by `1.1^7`. Once the real zero was crushed far enough, the fullwidth zero
was legitimately the argmax. Greedy decode preferred it *correctly*, given
logits that had been mangled before it saw them. That is why the corruption
appeared *inside numeric literals* and nowhere else, and why it never appeared in
either raw-decode probe: the probes did not set the penalty.

### A/B, two independent agents and two independent harnesses

Deliberately run twice, by different agents on different harnesses, because a
single harness had already been the problem twice this session:

| measurement | at `repetition_penalty = 1.1` | at `1.0` |
| --- | --- | --- |
| exact-match (harness A) | **0/90** | **25/90** |
| exact-match (harness B) | **0/120** | **33/120** |
| rows containing a homoglyph (A) | **2/100** | **0/100** |
| rows containing a homoglyph (B) | **4/120** | **0/120** |

And the mechanism measured directly rather than inferred: across **120/120 rows**,
**1,272 decode steps** had `sampler ≠ raw argmax` at 1.1, and **zero** at 1.0.
Every one of those 1,272 steps is a token the model chose and the sampler
replaced.

### Fix, in two places, deliberately

**Both services pin `repetition_penalty = 1.0F`** (commit `a1d6cdd`) — defence in
depth. A caller that does not want a penalty should say so rather than inherit a
default it did not choose, and this repo has already been burned once by a
`GenerationConfig` default (`compute_backend = AUTO` counting as a GPU request,
session 2026-08-01 §2). The lesson from that one was to fix it in the library
*and* in the caller so the fix does not depend on which sensen commit is pinned;
the same applies here.

**The library was fixed to dedup per unique token** — sensen `fb2723cd`
(originally `41ec1ee8`, rebased onto `2f68aba4`). That brings sensen into line
with HuggingFace and llama.cpp.

Its test scores **62/0** — 62 checks, 0 failures. More usefully, it carries a
**negative control**: with the dedup removed, rebuilt and rerun, the same suite
scores **34/62**, and gate 5 fails by **picking token 16 instead of token 15** —
the exact production symptom, reproduced on demand. A test that only passes proves
the code runs; a test whose removal reproduces the original defect proves the test
is measuring the right thing.

> **UNVERIFIED — open residual.** The mirrored CUDA kernel
> `apply_rep_penalty_batch_kernel` received the same dedup fix, and that fix is
> **UNCOMPILED and UNTESTED**. The local build is `ENABLE_CUDA=OFF` and `nvcc` is
> not on PATH, so nothing in this session executed a single line of it. It is
> written by analogy to the CPU path and reviewed by eye. Anyone enabling CUDA
> must treat that kernel as unproven and re-run the 62-check suite on the GPU
> path before trusting it. It is called out here rather than in a footnote
> because the CPU claim in this log is strong and the CUDA claim is not, and the
> two must not be read as one result.

### Greedy stays penalisable, on purpose

The obvious over-correction is to make the penalty skip greedy decode entirely.
That was checked against both references and rejected:

- **HuggingFace** runs its `LogitsProcessorList` — repetition penalty included —
  *before* greedy argmax.
- **llama.cpp** runs its penalty sampler *before* its greedy sampler.

So sensen's ordering is correct and matches both references. The trap was **the
1.1 default**, not the ordering. Changing the ordering would have made sensen
diverge from both engines it is checked against, to fix a bug that lives in a
different line.

### The DiffusionGemma question — asked, investigated, refuted

Before pushing anything, the owner asked whether the per-occurrence compounding
might have been **deliberate**, chosen for block-diffusion models where a token
recurring within a block means something different than it does in autoregressive
decode. If so, deduping would be a silent regression in the diffusion stack.

It is not, and three independent lines say so:

1. **Diffusion sampling does not go through this code.** It goes through
   `dlm::EntropyBoundSampler` / `sensen_cuda_eb_sample`.
2. **It cannot go through this code.** `dlm.cppm` imports only `std`,
   `sensen.diffusion_core`, `sensen.cpu_features` and `sensen.parallel`. It cannot
   *name* `Sampler`, let alone call it.
3. **The chronology forbids it.** The penalty was introduced in **`ad60b068`
   (2026-01-29)** as a bare positional loop with no comment justifying the
   accumulation. **The diffusion stack did not exist until 2026-06-11 — 4.5 months
   later.** It cannot have been written for a consumer that did not exist.

**Nothing was pushed until this was settled.** The question was reasonable, the
answer took real work, and "the loop has no comment" is not evidence of intent in
either direction — the git chronology is.

---

## 2. Model hosting: `railway up` cannot carry the models

Parallel thread, and the reason the fix above could not simply be deployed once it
was found.

### What was measured, not assumed

`railway up` **cannot carry a 639 MB model**. Measured, twice, with the sizes
recorded:

- **HTTP 413** at **1,229,621,349 bytes** (both models in the upload).
- **HTTP 413** again at **619,301,067 bytes** (one model).

The limit is real and it is not about bandwidth. **Repo context alone is 321 MB
and uploads fine**, and upstream throughput measured **8.35 MB/s** — the
`.railwayignore` comment claiming **2.4 MB/s** is stale and was left standing as a
reason not to try. Size is the constraint; speed is not.

**Volumes are disqualified**, for a structural reason rather than a preference:
Railway's own docs state *"Replicas cannot be used with volumes"*, and this
service runs **`numReplicas: 2`**. Attaching a volume means giving up the second
replica.

Two in-container fallbacks were tried and both are dead ends worth recording so
nobody re-tries them:

- **`railway ssh` stdin piping produces a 0-byte file.** It does not error. It
  produces a file of length zero, which is the worst possible failure mode
  because a `COPY` and a checksum check downstream will both fire on garbage.
- **The runtime container has no `curl` and no `wget`.** Nothing in it can pull a
  model at boot even if a URL existed.

### Solution: a private Railway bucket

`railway bucket` — native, S3-compatible, **private by default**, and **zero
dashboard actions required**, which matters because the account-scoped credential
problem documented in CLAUDE.md makes dashboard-dependent steps a recurring
blocker.

- Bucket **`model-artifacts`**, region **`sjc`**.
- Endpoint is **`t3.storageapi.dev`** — **not** the `*.storage.railway.app` the
  docs implied. Recorded verbatim because guessing it costs a build cycle.
- Both models uploaded and **round-trip verified**: re-downloaded and
  re-checksummed, not merely "the upload reported success".
- The build fetches with SigV4 (`curl --aws-sigv4`) and **verifies the pinned
  sha before use**, so a truncated or substituted object fails the build rather
  than shipping.

### Three self-inflicted deploy failures

All three are mine, all three produced a red build, and all three are the kind
that read as infrastructure problems:

1. **A dead `MODEL_URL` still pointing at a deleted HuggingFace repo.** The build
   **failed loudly**, which is the correct behaviour and is worth saying out loud
   — the checksum-and-fail design did its job. The defect was leaving the stale
   variable set, not the build's reaction to it.
2. **An empty `backend/models/` directory is not carried in the upload tar**, so
   the Dockerfile's `COPY` had no target and failed. Fixed with a `.gitkeep`. Git
   does not track empty directories and neither does the tar built from it.
3. **A global `ARG` was not redeclared inside the `model` stage**, so the SigV4
   branch evaluated its credential as empty and **silently fell through to
   unauthenticated `wget`**. This is *precisely* the trap the Dockerfile's own
   `MODEL_TOKEN` comment warns about, from the identical bug in session
   2026-08-01 §7 — an `ARG` that is in scope in one stage and invisible in the
   next. The comment was there. It was not read.

**Final state: both models in the container, checksums byte-exact, both assistants
LOADED.**

---

## 3. Five harnesses, each of which produced a confident wrong answer

Grouped together because they share one shape: **the tool returned a plausible
number for a question it was not being asked.** For each, the discriminator — the
cheap check that separates the true reading from the false one — is recorded,
since the finding is worthless without it.

**1. `evaluate.py` measures transformers on the merged bf16 intermediate.**
That is not what ships. Production is **Q8_0 GGUF through sensen**. Its own
sibling `agent/train/eval_grpc.py` says exactly this in its docstring. The numbers
**31.7%** and **36.4%** were quoted as the model's accuracy before this was
noticed; the real serving path scored **0/279**. A harness that measures the wrong
artifact does not produce a noisy answer, it produces a *confident* answer about a
different system.
*Discriminator:* if the harness never touches a `.gguf`, it is not measuring
production. Read the docstring of the sibling script before trusting the one you
picked.

**2. `railway logs --deployment <id>` silently returns a DIFFERENT deployment's
log.** The deployment id is **positional** (`railway logs <id> --build`), so the
`--deployment` flag is parsed as something else and the command falls back to the
latest deployment. This caused a **"Healthcheck succeeded!"** line from a
**two-day-old deployment** to be read as confirmation that a deployment which had
in fact failed was healthy. No error, no warning, correct-looking output.
*Discriminator:* the log's own **`image.created` metadata**. If it predates the
deploy you just triggered, you are reading a different deployment.

**3. `pgrep -f <pattern>` matches the checking command's own command line.**
Hit repeatedly. A training watcher reported "still running" for **40 minutes after
the run had completed**, because the watcher's own `pgrep -f` invocation contained
the pattern. Worse, a `pkill -f` on the same pattern **killed its own shell (exit
144)**. This is the same failure recorded in session 2026-08-03 and it recurred
anyway.
*Discriminator:* `pgrep -x <name>` — and note that **`comm` truncates to 15
characters**, so `calculator_engine` must be matched as **`calculator_engi`** or
it matches nothing and reports the process absent. Better still: capture the PID
once and use `kill -0 $PID`, which cannot match itself.

**4. `git log --all` includes `refs/original/`.** Those are `filter-branch`
backup refs. A sweep of "all refs" for `Co-Authored-By` therefore found **the very
commits that had been removed by the filter-branch**, and reported them as live
violations: **2 in the parent repo and 198 in sensen**. The true counts are **0 and
8**, and all **8 name the owner himself**, not an AI — they are legitimate
co-authorship trailers, not policy violations. The alarm was entirely an artifact
of searching refs that exist only as a safety net for the rewrite that fixed the
problem.
*Discriminator:* `git merge-base --is-ancestor <sha> HEAD`. If a commit is not an
ancestor of HEAD it is not in the history anyone will ever see.

**5. An engine without market-data credentials scores a working model as broken.**
The options assistant re-baselined at **6/14** during this session. With Alpaca
credentials present it is **16/16**. Without them `probe_symbol` returns
`Refusal.DATA_UNAVAILABLE`, and `eval_grpc.py` **silently scores that as
`got=None`** — indistinguishable, in the output, from a model that emitted
nothing. This is the third session in which this specific confusion has cost time.
*Discriminator, and the tell that was read straight past:* **SPY and NVDA rows
failed too.** Those are the most in-distribution symbols the model has. The
failure was being blamed on the known futures-root limitation (`ES`/`NQ` only),
which has nothing whatever to do with SPY. When a hypothesis fails to explain the
rows it should most obviously explain, it is the wrong hypothesis.

---

## What this session says about the last three

The 2026-08-03 log closed on: *verify identity (checksum, process, path) before
measuring, not after a result looks odd.* This session added a fourth axis to
that list — **configuration** — and it is the one that hides best. Checksum,
process and path can all be verified statically. A `GenerationConfig` default is
part of neither the artifact nor the environment; it is a value the caller never
wrote, in a struct the caller never looked at, applied at a point in the pipeline
(`sample`, before argmax) the caller had no reason to think about. The probe and
the service ran the *same model* on the *same engine* from the *same file* and
disagreed by 27 points, because one of them constructed a config and the other
did not.

Both `GenerationConfig` defaults that have now cost this project a debugging cycle
— `compute_backend = AUTO` and `repetition_penalty = 1.1F` — are library defaults
tuned for open-ended chat, inherited by a service doing constrained structured
extraction. That is the pattern. The service now pins both.

---

## Still open

- **The CUDA `apply_rep_penalty_batch_kernel` fix is UNCOMPILED and UNTESTED.**
  Local build is `ENABLE_CUDA=OFF`, `nvcc` not on PATH. Highest-priority residual
  in this log.
- The mortgage assistant is at **25/90** and **33/120** post-fix. That is a
  functioning model, not a shipped one; the pre-fix **0/279** simply made the real
  accuracy unmeasurable. The next question is how much of the remaining gap is
  model and how much is more of the same.
- `agent/train/evaluate.py` still measures the bf16 intermediate and still carries
  no warning in its own docstring — only its sibling does. Anyone who opens it
  first gets the same wrong answer.
- `eval_grpc.py` still collapses `Refusal.DATA_UNAVAILABLE` into `got=None`. Until
  it distinguishes them, every credential-less run scores every model identically
  and the score means nothing.
- Bucket credentials are build-time only. Nothing has verified what happens to a
  rebuild if the bucket credential is rotated.
