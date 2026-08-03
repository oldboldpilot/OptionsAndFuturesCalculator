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

## Result of record (2026-08-03)

Identical harness, single verified engine, raw-output layer, 16-row defect
holdout plus the two baseline regressions.

| Model | Holdout | Emitted `<params>` | Baselines |
| --- | --- | --- | --- |
| Deployed (QLoRA, 4 epochs) | **13/16** | 11/16 | both pass |
| Retrain (QLoRA, 2 epochs) | 5/16 | 3/16 | both fail |

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
2. Kill stray engines (`pkill -x calculator_engi`), confirm `:50051` is free.
3. Start one engine with `MODEL_PATH` at the candidate, `ASSISTANT_BACKEND=sensen`.
4. Assert exactly one process and one listener.
5. Confirm the startup line names the model you intended.
6. Run the holdout and the two baseline regressions.
7. Compare against the **deployed** model measured the same way in the same
   session — not against a number from a previous run or a different engine.
