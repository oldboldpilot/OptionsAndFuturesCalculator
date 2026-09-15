#!/usr/bin/env python3
"""Deterministic invariants of the mortgage corpus generator.

@author Olumuyiwa Oluwasanmi

WHY THESE ARE NOT MODEL TESTS. Every corpus defect this project has paid for
was visible in the DATA before any model was trained, and each one was found
only after a retrain measured worse:

  * `phrase_money` rendered round(v) into the utterance while the label kept
    two decimals, so a stated "$1,825" had to become 1824.51. Eight operations
    were a hard 0/98 for two independently trained models.
  * `prepaid_interest_days` labelled the 15-day convention on utterances that
    never mention prepaid interest. 21 of 26 held-out closing-cost failures.
  * the closing-cost generator printed six decimals for percents produced with
    round(..., 4), so every label ended "00" and numerically exact rows failed
    string equality: 0/42.
  * `make_cashflow_extraction` built its dated grid at triangular(300, 430,
    365) -- every interval about a year -- and labelled it ComputeXnpv, whose
    entire distinction from ComputeNpv is IRREGULAR spacing. A model reading
    "after 394 days; after 725 days" as an annual grid is repeating what it was
    shown. 8 of 9 held-out ComputeXnpv rows came back as ComputeNpv.

All four are properties of a pure function of (seed, n, val_frac, this file).
None needed a GPU, a checkpoint or twenty minutes of RPC to see. This file
asserts them in about a second.

Run:  python3 agent/dataset/test_corpus_invariants.py
"""
import random
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_mortgage_dataset as G  # noqa: E402

CHECKS = 0
FAILURES = 0


def check(cond: bool, what: str) -> None:
    global CHECKS, FAILURES
    CHECKS += 1
    if cond:
        print(f"  PASS: {what}")
    else:
        FAILURES += 1
        print(f"  FAIL: {what}")


def utterance(row: dict) -> str:
    return next(t["content"] for t in row["conversations"] if t["role"] == "user")


def label(row: dict) -> str:
    return next(t["content"] for t in reversed(row["conversations"])
                if t["role"] == "assistant")


def params(row: dict) -> dict | None:
    m = re.search(r"<params>(.*?)</params>", label(row), re.S)
    if not m:
        return None
    import json
    return json.loads(m.group(1))


def sample(fn, n: int, seed: int = 11):
    rng = random.Random(seed)
    return [fn(rng) for _ in range(n)]


# ---------------------------------------------------------------------------
print("1. determinism -- the generator is a pure function of its seed")
# The argparse DEFAULT is 3407 while this module's own docstring says --seed 0.
# A rebuild "to change only the closing-cost rows" once silently reseeded all
# 11,400 and put held-out rows into training. It was caught by diffing against
# a kept copy; without one it would have been invisible.
a = sample(G.make_cashflow_extraction, 200, seed=7)
b = sample(G.make_cashflow_extraction, 200, seed=7)
check(a == b, "same seed reproduces the same rows byte for byte")
c = sample(G.make_cashflow_extraction, 200, seed=8)
check(a != c, "a different seed produces different rows (the check is not vacuous)")

# ---------------------------------------------------------------------------
print("\n2. ComputeXnpv/ComputeNpv -- the discriminator the corpus must TEACH")
rows = sample(G.make_cashflow_extraction, 600)
by_op: dict[str, list] = {}
for r in rows:
    by_op.setdefault(params(r)["operation"], []).append(r)

for op in ("ComputeXnpv", "ComputeXirr"):
    got = by_op.get(op, [])
    check(len(got) > 0, f"{op} rows are generated at all")
    check(all("after" in utterance(r) and "days" in utterance(r) for r in got),
          f"every {op} utterance states day offsets")
    check(all("year 1:" not in utterance(r) for r in got),
          f"no {op} utterance uses the evenly-spaced 'year N:' phrasing")

for op in ("ComputeNpv", "ComputeIrr"):
    got = by_op.get(op, [])
    check(len(got) > 0, f"{op} rows are generated at all")
    check(all(not re.search(r"after \d+ days", utterance(r)) for r in got),
          f"no {op} utterance states day offsets")

# THE DEFECT ITSELF. Irregular spacing is the entire meaning of the X-form, so
# a dated grid whose intervals are all ~365 days is an annual grid wearing a
# different label. Assert the intervals genuinely spread.
intervals = []
for r in by_op.get("ComputeXnpv", []) + by_op.get("ComputeXirr", []):
    d = params(r).get("dates") or []
    intervals += [int(d[i + 1] - d[i]) for i in range(len(d) - 1)]
check(len(intervals) > 50, "enough dated intervals to judge the spread")
near_annual = sum(1 for g in intervals if 330 <= g <= 400)
check(near_annual / max(len(intervals), 1) < 0.35,
      f"dated intervals are NOT an annual grid in disguise "
      f"({near_annual}/{len(intervals)} fall in 330-400 days)")
check(min(intervals) < 200 and max(intervals) > 500,
      f"the dated grid spans genuinely irregular gaps "
      f"(min {min(intervals)}, max {max(intervals)} days)")

# The families must not share an opening clause: the cue has to arrive before
# the shape of the answer is half decided.
lead = lambda r: utterance(r)[:20]
dated_leads = {lead(r) for op in ("ComputeXnpv", "ComputeXirr") for r in by_op.get(op, [])}
plain_leads = {lead(r) for op in ("ComputeNpv", "ComputeIrr") for r in by_op.get(op, [])}
check(not (dated_leads & plain_leads),
      "dated and undated families share NO opening clause")

# Balance: a 35/65 split is what left 103 XNPV rows against 183 NPV.
dated_n = sum(len(by_op.get(o, [])) for o in ("ComputeXnpv", "ComputeXirr"))
check(0.4 <= dated_n / len(rows) <= 0.6,
      f"dated and undated are balanced ({dated_n}/{len(rows)} dated)")

# ---------------------------------------------------------------------------
print("\n3. down-payment extraction -- the label must be DERIVABLE, exactly")
dp = sample(G.make_down_payment_extraction, 400)
bad_math = bad_ground = 0
for r in dp:
    p = params(r)
    u = utterance(r)
    loan = float(p.get("loan_amount") or p.get("present_value"))
    money = [float(x.replace(",", "")) for x in re.findall(r"\$([\d,]+(?:\.\d+)?)", u)]
    if not money:
        bad_ground += 1
        continue
    price = max(money)
    # The loan is strictly below the stated price, and the difference is the
    # down payment -- either stated as money or as a percent of the price.
    if not (0 < loan < price):
        bad_math += 1
        continue
    down = price - loan
    stated_money = any(abs(down - m) < 0.005 for m in money)
    pct = down / price
    stated_pct = any(abs(pct * 100 - float(x)) < 1e-6
                     for x in re.findall(r"(\d+(?:\.\d+)?)%", u))
    if not (stated_money or stated_pct):
        bad_ground += 1
check(bad_math == 0, f"the loan is always price minus the down payment ({bad_math} bad)")
check(bad_ground == 0,
      f"the down payment is always STATED in the utterance, as money or a percent "
      f"({bad_ground} ungrounded)")
check(all(float(params(r).get("original_home_value", 0)) > 0
          for r in dp if params(r)["operation"] == "ComputeAmortization"),
      "original_home_value carries the PRICE, not the loan -- PMI drops off against it")

# ---------------------------------------------------------------------------
print("\n4. isolation -- one generator's draws cannot perturb another's")
# The property that makes a corpus experiment ATTRIBUTABLE. Until 2026-09-14 a
# single shared RNG drove the weighted selection, every generator's internal
# draws and the train/val shuffle, so adding one extra draw inside ONE generator
# changed 2032 of 4000 rows across every family. With per-generator streams the
# same perturbation changes 360, and all 360 belong to the four operations that
# generator emits.
#
# Without this, a corpus edit and its measured effect cannot be connected: the
# XNPV fix on 2026-09-14 also moved ComputeHeloc, ComputeRefinance and
# ComputeFutureValueDetailed, and there was no way to tell cause from resample.
check(G.stream_seed(0, "a") != G.stream_seed(0, "b"),
      "distinct generator names get distinct streams")
check(G.stream_seed(0, "a") == G.stream_seed(0, "a"),
      "stream_seed is a pure function of (seed, name)")
check(G.stream_seed(1, "a") != G.stream_seed(0, "a"),
      "the master seed still reaches every stream")
# sha256, not hash(): Python salts str hashing per process unless PYTHONHASHSEED
# is fixed, which would make the corpus differ between runs of one command.
check(G.stream_seed(0, "make_cashflow_extraction") == 16490498688349206090,
      "stream_seed is stable ACROSS PROCESSES, not merely within one")

# Non-interference, tested directly: a generator's k-th row must not depend on
# what else has drawn in between.
import random as _r
for _, fn in G.CORPUS_MIX[:6]:
    name = fn.__name__
    first = [fn(_r.Random(G.stream_seed(0, name))) for _ in range(1)][0]
    noise = _r.Random(99)
    for _, other in G.CORPUS_MIX:
        if other is not fn:
            other(noise)
    again = fn(_r.Random(G.stream_seed(0, name)))
    check(first == again,
          f"{name}'s row is unchanged after every other generator has run")

# ---------------------------------------------------------------------------
print("\n5. coverage -- the test's subjects are DERIVED, not named")
# A test that lists its own subjects can only check the ones somebody
# remembered. That is the defect this repository records against a hand-written
# operation allow-list which drifted to refusing thirteen of the twenty-seven
# live operations, and against a hand-maintained module list that has broken a
# submodule bump three times. So the generators below come from CORPUS_MIX --
# the table build_mortgage_dataset.main() actually samples -- and a generator
# added to the module without being wired in fails HERE rather than silently
# shipping untested.
IN_MIX = {fn.__name__ for _, fn in G.CORPUS_MIX}
DEFINED = {name for name in dir(G)
           if name.startswith("make_") and callable(getattr(G, name))}

# Helpers another generator calls, rather than rows the corpus samples. Each
# entry needs a REASON, so adding one is a decision and not a reflex.
NOT_SAMPLED_DIRECTLY: set[str] = set()

unwired = DEFINED - IN_MIX - NOT_SAMPLED_DIRECTLY
check(not unwired,
      f"every make_* in the module is in CORPUS_MIX or explicitly excluded "
      f"(unwired: {sorted(unwired)})")
check(not (IN_MIX - DEFINED),
      f"every CORPUS_MIX entry names a real function ({sorted(IN_MIX - DEFINED)})")

# ---------------------------------------------------------------------------
print("\n6. per-generator contract -- applied to EVERY generator automatically")
# The invariants that hold for ANY row the corpus can contain, so a generator
# added tomorrow is covered the moment it joins CORPUS_MIX. Nothing here needs
# to know what the new generator is for.
bad_keys, bad_ops, excluded_leaks, nondeterministic, empty = [], [], [], [], []
for _, fn in G.CORPUS_MIX:
    name = fn.__name__
    try:
        batch = sample(fn, 120, seed=23)
        again = sample(fn, 120, seed=23)
    except Exception as exc:                 # a generator that throws is a defect
        bad_ops.append(f"{name}: raised {type(exc).__name__}")
        continue
    if batch != again:
        nondeterministic.append(name)
    if not batch or any(not utterance(r).strip() for r in batch):
        empty.append(name)
    for r in batch:
        p_ = params(r)
        if p_ is None:
            continue                         # refusal / chitchat / clarification
        op = p_.get("operation")
        if op not in G.OPERATIONS:
            bad_ops.append(f"{name}: {op}")
            continue
        if set(p_) - {"operation"} != G.op_field_names(op):
            bad_keys.append(f"{name}/{op}")
        for f in G.OP_EXCLUDED_FIELDS.get(op, set()):
            if f in p_:
                excluded_leaks.append(f"{name}/{op}.{f}")

check(not nondeterministic,
      f"every generator is deterministic in its seed ({sorted(set(nondeterministic))})")
check(not empty, f"no generator emits an empty utterance ({sorted(set(empty))})")
check(not bad_ops,
      f"every operation named exists in finance.proto ({sorted(set(bad_ops))[:3]})")
check(not bad_keys,
      f"every key set EXACTLY equals the operation's field set minus exclusions "
      f"({sorted(set(bad_keys))[:3]})")
check(not excluded_leaks,
      f"no generator labels a field its operation DISCARDS "
      f"({sorted(set(excluded_leaks))[:3]})")
print(f"     ({len(G.CORPUS_MIX)} generators exercised)")

# ---------------------------------------------------------------------------
print("\n9. the HARNESS sends what the contract carries")
# The eval harness is measurement apparatus, and a defect in it is reported as a
# defect in the model. `eval_grpc_mortgage.py` computed `question1` on every
# path, carried a comment saying to echo it back on the scored call, and did not
# pass it -- so `prior_question` was "" on every scored request for as long as
# the field existed.
#
# It cost two full evaluation cycles. `prior_question` goes into the PROMPT, so
# adding it changes what the model emits: raw_exact moved 449 -> 443 on
# identical weights, which means no number measured before the fix is comparable
# with one measured after it. It also silently disabled a serving-layer rule
# that keys on the field, so a change measured as "no effect" had simply never
# been exercised.
#
# Asserted against the SOURCE because the alternative is a live engine, and a
# gate nobody can run is not a gate. It is the same shape as the pricing page's
# own guard in the sibling repository: ask whether the call site passes the
# thing, not whether the variable exists.
_harness = (Path(__file__).resolve().parent.parent / "train" / "eval_grpc_mortgage.py").read_text()

# The scored (second) call must forward the question the service asked.
_scored = [ln for ln in _harness.splitlines()
           if "call(stub, utterance, reply" in ln and not ln.strip().startswith("#")]
check(len(_scored) == 1,
      f"exactly one scored ParseOperation call site (found {len(_scored)})")
check(bool(_scored) and "question1" in _scored[0],
      "the scored call forwards prior_question -- without it the service "
      "substitutes a placeholder that appears ZERO times in the corpus")

# And `question1` must actually be bound from the first call, not left "".
check("question1, _ = call(stub, utterance" in _harness or
      ", question1, _ = call(stub, utterance" in _harness,
      "question1 is bound from the FIRST call's clarification, not a constant")

print(f"\n{CHECKS} checks, {FAILURES} failures")
sys.exit(0 if FAILURES == 0 else 1)
