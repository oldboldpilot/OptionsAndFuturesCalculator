# Reasoning over an assistant evaluation

@author Olumuyiwa Oluwasanmi

An aggregate score cannot answer a coverage question, and every scoring defect
this project has recorded was a coverage defect. `400/508 = 78.7%` is the
number quoted for the mortgage model of record, and it is silent on whether one
operation is `0/11` — yet `ComputePresentValue` went 0/11 → 11/11 on nothing but
adapter rank, and `ComputeRentVsBuy` was 0/18 because the previous model could
not serve that operation at all. Both were invisible in the total.

So the evaluation is turned into Prolog facts and queried.

```
# 1. serve the candidate, exactly one engine on :50051
MORTGAGE_MODEL_PATH=<candidate>.gguf ASSISTANT_BACKEND=sensen PRO_GATE_MODE=off \
    ./backend/build/calculator_engine > engine.log 2>&1 &
pgrep -x calculator_engi | wc -l          # MUST be 1 -- SO_REUSEPORT splits otherwise

# 2. score through the REAL RPC, refusing a contaminated holdout
python3 agent/train/eval_grpc_mortgage.py --addr localhost:50051 \
    --val val.jsonl --engine-log engine.log \
    --assert-disjoint-from train.jsonl --json-out eval.json

# 3. derive the facts and ask the coverage questions
python3 scripts/emit_assistant_facts.py val.jsonl eval.json facts.pl
<sensen-build>/bin/assistant_check facts.pl
```

`assistant_check.cpp` is kept here because it is OUR question set; it builds
inside sensen's own test tree (`tests/CMakeLists.txt`,
`add_executable(assistant_check assistant_check.cpp)` linking
`${SENSEN_TEST_LINK}`), because it imports `sensen.logic` and
`sensen.logic_parser`, which `sensen_slim` deliberately excludes.

Neither the facts nor the questions are hand-written against each other: the
facts come from the holdout, the eval output and `finance.proto`, and the
questions come from this file. A rule base maintained by hand beside the thing
it describes has the identical drift problem it exists to detect.

## What it found on its first run (mortgage-v13, 2026-09-14)

| check | result |
| --- | --- |
| every operation has holdout rows | OK — 27/27 |
| emitted operations are real RPCs | OK — nothing invented |
| no operation scores zero | **FAIL** — 4 operations |
| excluded fields are never emitted | **FAIL** — 26 cases |
| the named operation is the gold one | **FAIL** — 14 cases |

**Three of the four zeros were the MEASUREMENT, not the model.** `ComputeRate`
0/7, `ComputeIrr` 0/6 and `ComputeXirr` 0/6 fail on `guess` (and XIRR's `rate`)
alone — fields `OP_EXCLUDED_FIELDS` declares the operation discards and
`mortgage_assistant_service.cpp` DROPS before grounding ever sees them. Scored
the way production serves, all three are perfect. Pooled: 465/567 = 82.0% raw,
**485/567 = 85.5% production-equivalent**.

**The fourth was real.** `ComputeXnpv` is 0/9, and 8 of those 9 are answered
`ComputeNpv` — the model names the wrong operation and drops the dates. That is
not a field slip: NPV over evenly-spaced periods and XNPV over dated flows are
different questions, and the engine answers the wrong one plausibly. Exactly the
failure the aggregate hides.
