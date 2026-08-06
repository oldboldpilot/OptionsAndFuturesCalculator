# Evaluating a Strategy-Assistant Model

@author Olumuyiwa Oluwasanmi

How the strategy assistant is actually served, and how to measure a candidate
model against the engine that serves it. Written after a retrain was commissioned
to fix a regression that did not exist, because the measurement was taken on an
engine that never handles a request.

## The serving path

The assistant runs **in-process inside `calculator_engine`**, on sensen. There is
no separate model server and no subprocess.

```
gRPC ParseStrategy
  -> assistant_service.cpp   build_prompt()  (system prompt + turns)
  -> AssistantWorker         one owner thread
  -> SensenBackend           sensen::LLMPipeline::fromGGUF(MODEL_PATH)
  -> [assistant] raw model output          <-- logged HERE, pre-verification
  -> assistant_verification.cppm           symbol/strategy/ambiguity checks
  -> ParseResponse { params | clarification | refusal }
```

Configuration, all environment variables read at startup:

| Variable | Default | Notes |
| --- | --- | --- |
| `MODEL_PATH` | none | Absolute path to the Q8_0 GGUF. Unset ⇒ the assistant returns a Refusal on every call. |
| `ASSISTANT_BACKEND` | `sensen` | `sensen` or `llamacpp`. Production runs `sensen`. |
| `ASSISTANT_CONTEXT_TOKENS` | 4096 | KV cache is F16/Q8; see CLAUDE.md. |
| `ASSISTANT_INFERENCE_THREADS` | 4 | |
| `ASSISTANT_MAX_CONCURRENT` | 4 | Scheduler slots, not threads. |
| `ASSISTANT_QUEUE_DEPTH` | 8 | |

The backend selection is deliberately not self-correcting: asking for a backend
that fails to initialise leaves the assistant unavailable rather than quietly
starting the other one. Confirm which one is live from the startup line, never
from the config you *think* is deployed:

```
Strategy assistant ready: backend=sensen model=/app/model/strategy-assistant.gguf ...
```

## Do not evaluate with llama.cpp

`llama-cli` measures an engine that never serves a request. It also differs in
tokenizer path, sampling defaults, KV dtype and prompt assembly, so its answers
are not a proxy for what a user gets.

This is not hypothetical. A `llama-cli` holdout run scored the **deployed** model
7/16 and triggered a full retrain to fix the "regression". Measured through
sensen, that same deployed model scores **13/16** with both baselines passing.
The retrain built to fix the phantom scored **5/16**. The regression was in the
measurement.

`llama-cli` also drops into interactive mode here despite `-no-cnv`, blocking on
stdin until the timeout — which reads as "the model is slow" rather than "the
harness is wrong".

**The llama.cpp rule is one instance of a family, not a special case.** The
general shape is: a command succeeds, returns plausible data, and answers a
slightly different question than the one you asked. The next section catalogues
five more of them, all found on 2026-08-05, each of which produced a confident
wrong answer that survived review until something unrelated contradicted it.

## Measurement traps that produce a confident wrong answer

Five traps, measured 2026-08-05. Each is recorded with its symptom, the wrong
conclusion it produced, and the **discriminator** — the check that settles it,
chosen because it comes out *differently* if the assumption is wrong.

### 1. `evaluate.py` measures the wrong artifact AND the wrong engine

**Symptom.** `agent/train/evaluate.py` reports a plausible exact-match score in
seconds and never mentions what it loaded. What it loads is the merged **bf16**
directory through **transformers**:

```python
model = AutoModelForCausalLM.from_pretrained(a.model, dtype=torch.bfloat16, device_map="cuda")
```

Production serves a **Q8_0 GGUF through sensen**, with a q8 KV cache, behind
`assistant_service.cpp`'s own `build_prompt()` and `kSystemPrompt`. Weight
quantization, KV quantization and the real prompt assembly all sit between that
number and a user.

**Wrong conclusion.** "The model scores 31.7%", later "36.4%" — figures used to
argue about whether to retrain. The same weights through the real RPC scored
**0/279**. Nothing about the retrain question was answerable from the transformers
number, in either direction.

**Discriminator.** `agent/train/eval_grpc.py` (options) and
`agent/train/eval_grpc_mortgage.py` (mortgage). Both drive the real RPC against a
running `calculator_engine`. `eval_grpc.py`'s own docstring already said this in
so many words — "evaluate.py measures the merged 16-bit weights straight out of
training … but NOT what ships" — and was not read.

**The nuance that matters.** The two harnesses are not redundant, and neither
supersedes the other. `evaluate.py` is a fast check on whether the LoRA adapter
learned the task at all; `eval_grpc.py` is what a trader gets. **A gap between
them is itself a finding.** Here the gap was 36 points and the cause was a
serving-layer defect, not a model difference — which is exactly the thing a
single-harness measurement can never surface. Run both, and treat a divergence as
a bug to locate rather than noise to average away.

### 2. An engine without market-data credentials scores a working model as broken

**Symptom.** The options assistant re-baselined at **6/14 params + 2/2
non-params** on the 16-row defect holdout. With Alpaca credentials present the
identical model on the identical build is **16/16** — 14/14 params, 2/2
non-params, 3/3 asked-when-ambiguous.

`probe_symbol` (`backend/src/modules/assistant_service.cpp:1761`) needs a live
equity snapshot for **every** symbol except an already-disambiguated non-EQUITY
root, which is the one case that falls through to catalogue membership. Without
credentials `market_data::fetch_quote` fails to resolve, the RPC returns
`Refusal.DATA_UNAVAILABLE`, and `eval_grpc.py` records that as `got=None`.

**Wrong conclusion.** "The model regressed" / "the futures training gap is worse
than documented."

**The tell that was read past.** **SPY and NVDA rows failed too** — plain equity
rows, nothing to do with futures. Anchoring on a *documented* limitation (the
`ES`/`NQ`-only futures roots) supplied a ready explanation for some of the
failures and stopped the search before it reached the ones that explanation could
not cover. A known limitation is the most dangerous kind of hypothesis: it is
already believed, so it is not tested.

**How the scoring makes it worse, not just silent.** In `eval_grpc.py`, `call()`
returns the refusal *message* (line 107) and all three call sites discard it into
`_` (lines 132, 138, 144); `resp.refusal.reason` is never read at all. Then:

- a params row scores as a **miss** (`got=None != want`);
- a non-params row scores as a **pass** — line 155 credits any outcome that is
  not `params`, and a `DATA_UNAVAILABLE` refusal is not `params`;
- an ambiguity row's ask sub-check also scores as a **pass**, for the same reason
  at line 140.

So an engine with no credentials at all still reports `2/2 non-params` and
`3/3 asked`. The two components that looked healthy were healthy *for free*.

**Discriminator, and the fix to make.** `eval_grpc.py` must print the refusal
reason on every mismatch and **hard-fail** any run containing a
`DATA_UNAVAILABLE` or `MODEL_UNAVAILABLE` refusal. An infrastructure refusal
scored as a model miss is indistinguishable from a real failure, and the run
should refuse to produce a number rather than produce a wrong one. The sibling
`eval_grpc_mortgage.py` already does the reporting half — it carries
`resp.refusal.reason` out of `call()` (line 275), buckets it in
`classify_refusal()`, and prints a refusal-shape histogram — so this is porting
existing code, not designing it.

Until that lands, the pre-existing rule below covers you: score the raw model
output (see "Score the raw model output when market data is unavailable"), or
confirm credentials are live before believing any RPC-layer number.

### 3. `pgrep -f <pattern>` matches the checking command's own command line

**Symptom.** A training-completion watcher polled
`pgrep -c -f "train.py --method qlora"` and reported "still running" for **40
minutes** after the run had finished — because the shell executing the `pgrep`
carried that literal string in its own command line and matched itself. A
`pkill -f` cleanup in the same family matched its own command and killed the
shell it ran in, surfacing as an unexplained exit 144.

**Wrong conclusion.** "The training run is still going." Nothing contradicted it;
a human eventually asked.

**Why this is the worst-shaped failure of the five.** A watcher that wrongly
reports *finished* is caught within seconds, because the next step finds no
artifact. A watcher that wrongly reports *still running* is indistinguishable
from patience. It has no natural contradiction and burns wall clock until someone
interrupts it. Bias polling checks so that the failure mode is the loud one.

**Discriminator.**

- `pgrep -x <name>` — exact match on `comm`, which cannot match the pattern
  string because the pattern is not the process name.
- `comm` is truncated to **15 characters**, so `calculator_engine` must be matched
  as `calculator_engi`, and `pkill -x calculator_engine` matches **nothing**
  while appearing to succeed.
- When the distinguishing text exists *only* in the arguments — as it does for
  `train.py --method qlora`, where `comm` is just `python3` — do not pattern-match
  at all. **Capture the PID at launch and poll `kill -0 $PID`.** It is exact, it
  cannot match the poller, and it cannot be confused by a recycled name.

### 4. `railway logs --deployment <id>` can silently show a DIFFERENT deployment

**Symptom.** A `Healthcheck succeeded!` line was read as confirmation that a
just-failed deploy had worked. It came from a deployment two days old.

**The mechanism, as verified against `railway 4.35.0`.** The deployment id is
**positional**, not a flag value:

```
Usage: railway logs [OPTIONS] [DEPLOYMENT_ID]
  [DEPLOYMENT_ID]  Deployment ID to view logs from. Defaults to most recent
                   successful deployment, or latest deployment if none succeeded
  -d, --deployment  Show deployment logs        <-- a BOOLEAN, takes no value
```

`--deployment` is a boolean "show deployment logs" switch. Two consequences:

- `railway logs --deployment <id> --build` **hard-errors** on this version
  (`the argument '--deployment' cannot be used with '--build'`), and the natural
  recovery — drop the flag that was named in the error — drops the id along with
  it in the reader's head.
- With **no** id bound, the documented default is *the most recent **successful**
  deployment*. After a deploy that just failed, that default is by construction a
  stale, healthy one. The command succeeds, the log is real, and it describes
  different code.

**Honest correction to the original report.** The claim that the flag form
*silently* rebinds to the most recent successful deployment does **not**
reproduce on 4.35.0: `railway logs --deployment <bogus-uuid> --lines 2` answers
`Deployment not found`, so at this version the id does reach the positional. The
load-bearing and independently verified fact is the **default** — an unbound id
means "most recent successful", which is exactly the wrong deployment to be shown
when you are investigating a failure. Treat the flag form as a smell, and pass
the id positionally: `railway logs <id> --build`.

**Discriminator, and the stronger rule.** The build log's own
`containerimage.descriptor` decodes to an
`org.opencontainers.image.created` timestamp — read that, not the presence of a
success line. And more generally: **verify a deploy behaviourally.** Send a
request whose answer differs between the old and the new code and check which
answer comes back. `railway up` exiting 0 means *uploaded*, not *deployed*; a log
line means *some* deployment was healthy, not *this* one.

### 5. `git log --all` includes refs that are not part of history

**Symptom.** A sweep for `Co-Authored-By` across all refs found the very commits a
prior history rewrite had **removed**.

**Wrong conclusion.** "2 live policy violations in the parent repo and 198 in
sensen." The true live counts are **0** and **8**.

**Verified, 2026-08-06:**

| repo | `git log --all --grep` | reachable from `HEAD` | where the rest live |
| --- | --- | --- | --- |
| parent | 2 | **0** | `refs/original/refs/heads/master` (filter-branch backup) |
| `backend/sensen` | 198 | **8** | `refs/remotes/{origin,gitea}/preserved/pinned-*`, and the stale local branch `subdirectory-integration-fixes` |

Two things this table says that the original report did not. First, sensen has
**no `refs/original/` at all** — its 190 unreachable hits are preserved/pinned
backup branches, so the trap is broader than filter-branch: `--all` means every
ref, including deliberate backups, stale topic branches, and remotes you do not
build from. Second, all **8** genuinely live sensen commits carry
`Co-Authored-By: Olumuyiwa Oluwasanmi <muyiwamc2@gmail.com>` — the repository
owner himself, not an AI agent. The finding as originally reported was wrong in
its count and wrong in its subject.

**Discriminator.** Reachability, not existence:

```bash
git merge-base --is-ancestor <sha> HEAD
git merge-base --is-ancestor <sha> <remote>/master
```

A commit that exists in the object database is not a commit that is in your
history. If you are auditing what ships, ask what is an ancestor of what ships.

## The unifying lesson

Every one of these commands **succeeded**. Every one returned real, plausible,
non-empty data. Every one answered a slightly different question than the one
being asked:

| you asked | it answered |
| --- | --- |
| how good is the served model? | how good are the bf16 weights under transformers |
| did the model get this row right? | did the engine have credentials |
| is training still running? | is this pattern in some process's argv, including mine |
| did my deploy work? | was some deployment healthy at some point |
| does history contain this? | does the object database contain this |

Exit code 0 and non-empty output are not evidence that you measured what you
intended to measure. Before trusting a number, state — out loud, in the write-up —
**what artifact, what engine, what environment, and what scoring rule** produced
it. If any of the four is a guess, the number is a guess.

Then pick a discriminator that would come out **differently** if your assumption
were wrong. `pgrep -f` returning a count does not discriminate, because it returns
the same count whether or not the job is running. `kill -0 $PID` does. A green log
line does not discriminate between deployments; a behavioural probe does. This is
the whole method: not "check twice", but "check with something that can disagree".

The corollary, from trap 2: **when a partial explanation is available, it will end
the search.** The futures-root limitation is real and documented, so it absorbed
the futures failures and the equity failures were never accounted for. Before
accepting a known limitation as the cause, confirm it explains *every* failing
row. The rows it does not explain are the finding.

## Verify WHICH model production serves, by checksum

The GGUF sitting on the training host is not necessarily the one production runs.
Production fetches its model at image build time from the URL pinned in
`MODEL_URL` and verifies it against `MODEL_SHA256`. Read those, and check the
candidate's `sha256sum` against them before calling anything "the deployed model":

```bash
railway variables --service options-calculator-backend --kv | grep -E '^MODEL_(URL|SHA256)='
sha256sum /path/to/candidate.gguf
```

Measured 2026-08-03: `/scratch/agents/gguf_v2/param-agent-qlora-v2-Q8_0.gguf`
(`eab97cf5…`) had been treated as "the deployed model" for a whole session of
measurements. Production was pinned to `91d4ea5d…` — a different file, and a
materially worse one: **6/16 against the candidate's 16/16** on the identical
engine build and harness. The better model had been built and left unused while
production served the weaker one.

To change it: upload the GGUF to the HF repo, set `MODEL_URL` and `MODEL_SHA256`
together, redeploy. Re-download and re-checksum after uploading — the file that
was measured and the file that gets served must be provably the same bytes. The
worked procedure is in `docs/STRATEGY_ASSISTANT_PIPELINE.md` §4 ("Swapping the
served model"); the full provenance of the model serving today — recipe,
hyperparameters, losses, timings, checksum — is §2b ("Model of record").

## The measurement harness

Drive the real RPC. `MODEL_PATH` points at the candidate; everything else is the
production configuration.

```bash
MODEL_PATH=/path/to/candidate-Q8_0.gguf ASSISTANT_BACKEND=sensen \
  ./backend/build/calculator_engine
```

Then call `calculator.assistant.StrategyAssistant/ParseStrategy` with
`ParseRequest{utterance, prior_clarification}`.

**`prior_clarification` carries the trader's ANSWER, not the question.**
`build_prompt` emits it as a *user* turn following a placeholder assistant
question. The proto comment describing it as "the single short question this
service asked" is stale. For a holdout row shaped
`[system, user, assistant(question), user(answer), assistant(params)]`, the first
user turn is `utterance` and the second is `prior_clarification`.

### Assert exactly one engine — this is not optional

Engines bind `:50051` with `SO_REUSEPORT`. Start several and they **all** listen
simultaneously while the kernel load-balances requests across them. With
different models loaded, results interleave across models and the output is
incoherent in ways that mimic real bugs: a SPY iron-condor prompt came back
answered with bond-futures text, which reads exactly like KV-cache bleed or a
corrupted checkpoint. Five engines had accumulated.

Before trusting any number:

```bash
pgrep -x calculator_engi | wc -l      # must be 1
ss -ltn | grep -c ':50051'            # must be 1
```

Two traps in the cleanup itself:

- The process `comm` is truncated to 15 characters, so `pkill -x
  calculator_engine` matches **nothing** and silently leaves engines running.
  Use `pkill -x calculator_engi`.
- Never `pkill -f` on the binary path. The launching shell's own command line
  contains that path, so the shell kills itself — it surfaces as an unexplained
  exit 144.

Re-confirmed 2026-08-05, and generalised: the self-match is not specific to the
engine binary. `pgrep -f` and `pkill -f` match the *checking* command's own argv
for any pattern, which is trap 3 above. Both the assertion and the cleanup here
must use `-x` against the 15-character-truncated `comm`.

### Score the raw model output when market data is unavailable

Symbol verification calls live market data. Without Alpaca credentials the RPC
returns a refusal or a clarification **regardless of what the model produced**, so
every model scores identically — a measurement of the verification layer, not the
model. Observed: both a good and a bad model scored exactly 3/16 that way.

The engine logs `[assistant] raw model output` *before* verification runs. Parse
the `<params>` block out of that line and score it. With credentials present,
score the RPC result instead and you additionally exercise verification.

Note the two layers answer different questions. Raw output measures the model.
The RPC result measures the model *plus* the verification and aliasing layer,
which is what a user experiences.

Re-confirmed 2026-08-05, with a sharper number and a sharper warning. The
degradation is not uniform and not always downward: without credentials the
options assistant scored **6/14 params but 2/2 non-params and 3/3 asked**,
because `eval_grpc.py` credits any non-`params` outcome on those rows and a
`DATA_UNAVAILABLE` refusal qualifies. The same model with credentials is
**16/16**. So the earlier "every model scores identically" is the optimistic
case — the realistic case is a *partially* healthy-looking scorecard whose
healthy-looking components are the ones being scored wrong. Trap 2 above has the
line references and the fix.

## Result of record (2026-08-03)

Identical harness, single verified engine, raw-output layer, 16-row defect
holdout plus the two baseline regressions.

Raw-output layer, before the engine fixes below, on the candidate GGUF
(`eab97cf5…`):

| Model | Holdout | Emitted `<params>` | Baselines |
| --- | --- | --- | --- |
| QLoRA, 4 epochs (`eab97cf5`) | **13/16** | 11/16 | both pass |
| QLoRA, 2 epochs (`b70fd737`) | 5/16 | 3/16 | both fail |

RPC layer, after the engine fixes, single verified engine, live market data:

| Model | Holdout |
| --- | --- |
| `eab97cf5` — **now deployed** | **16/16** |
| `91d4ea5d` — what production served until 2026-08-03 | 6/16 |

The two columns are not complements, and the arithmetic looks wrong until you see
why: **2 of the 16 rows correctly emit no `<params>` at all** — they are ambiguity
rows where asking a question *is* the pass condition. So for the deployed model,
11 rows emitted parameters, 2 correctly asked instead, and 3 wrongly stayed silent
(11 + 2 + 3 = 16), giving 13 correct.

The 2-epoch retrain was **not deployed**. Halving the epochs made it materially
worse, which also refutes the working theory that 4 epochs had overfit.

### The holdout now passes 16/16 — with no retrain

Every remaining failure turned out to be an engine defect, not a model one. The
deployed model was unchanged throughout; three fixes took it from 13/16 to
**16/16** at the RPC layer:

1. **Bare futures directives were refused.** The model reads "Long NQ, 45 days,
   2 contracts." as a request to PLACE a trade and answers in prose, emitting no
   params. `recover_bare_futures_directive` (assistant_verification.cppm) parses
   that one shape deterministically and feeds the result through the SAME
   validation and GP-ARA gate the model's own output faces. Fixed NQ, GC and
   `gold outright long`.
2. **An unclosed `<think>` swallowed a correct answer.** `strip_think_block`
   dropped everything when `</think>` was missing, on the assumption that meant
   truncation. The model actually emits `<think>\n\n<params>{...}</params>` --
   130 bytes, `</params>` balanced, simply missing the closing think tag. A
   complete answer was being discarded as unparseable. Fixed the NVDA baseline.
3. **The five two-expiry strategies were unreachable.** `StrategyParams` had one
   `expiration_days`, so the verifier refused calendar, diagonal, double
   diagonal, PMCC and futures calendar outright -- strategies the calculator
   prices and sells. A trader asking "calendar spread on CL, 45 days" got a
   refusal. `far_expiration_days` was added as an optional field; a near leg
   alone is now accepted and the UI completes the far one from a second chain,
   exactly as `StrategySelector.tsx` already does. What is still refused is a far
   leg at or before the near leg, which no second chain can repair.

Worth noting for its own sake: fix 3 also made `crude oil calendar spread, 60
days` resolve its ambiguous `CL` root. The ambiguity inference had always been
able to settle it from the Futures category plus lexical support, but the
multi-expiry refusal fired first, so that path was dead for all five strategies.

## Dataset facts worth keeping

**Two dataset directories exist and they are not the same.** Training consumes
`agent/dataset/data/` (`--data ../dataset/data`); `agent/dataset/out/` is whatever
`build_dataset.py` last wrote locally. Measure the one the run actually consumed —
checking `out/` while the model trained on `data/` describes a file the model
never saw.

| | rows | clarification | modification | single-turn | strategies (min/median/max) |
| --- | --- | --- | --- | --- | --- |
| `data/` (**trained on**) | 28,500 | **18.5%** | 15.3% | 66.2% | 47 (546 / 605 / 657) |
| `out/` (generated) | 11,400 | 16.5% | 14.6% | 68.9% | 47 (186 / 217 / 558) |

The training set is well balanced: 47 strategies, none below 546 rows. Coverage
was never the problem, and neither was the clarification share.

An earlier "31.1% clarification, roughly double the recipe" figure was wrong twice
over: it counted every multi-turn row (`make_modification` is also four-turn), and
it was computed against `out/`. Separate the two by whether the *first* assistant
turn contains a `<params>` block.

## Checklist

1. Build the candidate to Q8_0 GGUF (`scripts` on the training host; see
   `docs/STRATEGY_ASSISTANT_PIPELINE.md`).
2. Kill stray engines (`pkill -x calculator_engi` — **not** `-f`, **not** the
   untruncated name), confirm `:50051` is free.
3. Start one engine with `MODEL_PATH` at the candidate, `ASSISTANT_BACKEND=sensen`.
4. Assert exactly one process and one listener (`pgrep -x calculator_engi | wc -l`,
   `ss -ltn | grep -c ':50051'`).
5. Confirm the startup line names the model you intended, and its `sha256sum`
   matches what production pins.
6. **Confirm market-data credentials are live** before believing any RPC-layer
   number — or score the raw-output layer instead. A run containing any
   `DATA_UNAVAILABLE` or `MODEL_UNAVAILABLE` refusal has no valid score.
7. Run the holdout and the two baseline regressions through
   `agent/train/eval_grpc.py` — the real RPC. If you also ran
   `agent/train/evaluate.py`, record both and treat any gap as a defect to
   locate, not an average to take.
8. Compare against the **deployed** model measured the same way in the same
   session — not against a number from a previous run or a different engine.
9. Before writing the number down, state the four attributes: artifact, engine,
   environment, scoring rule.

## Related

- `docs/STRATEGY_ASSISTANT_PIPELINE.md` — training-to-serving chain, model of
  record, and the procedure for swapping the served model.
- `docs/MORTGAGE_ASSISTANT_PIPELINE.md` — the mortgage assistant's equivalent,
  whose harness `agent/train/eval_grpc_mortgage.py` already reports refusal
  reasons and separates raw-model accuracy from post-verification accuracy.
- `docs/MORTGAGEFV_INTEGRATION.md` and `clients/mortgagefv/` — the downstream
  client of that contract.
- `docs/session_logs/session_2026-08-05_penalty_hosting_and_harness.md` — the
  session in which the five traps above were found.
