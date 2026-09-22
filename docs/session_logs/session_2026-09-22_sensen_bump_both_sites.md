# Session 2026-09-22 — sensen `12fe563d` → `9c760d4f` for both websites

@author Olumuyiwa Oluwasanmi

## What prompted it

A stale-looking `origin/feat/pmi-termination-policy` turned out to be a **sensen**
branch, already retired that day by `d117a16f` — a DELIBERATELY EMPTY merge.
The branch carried one commit, `3bec86fe` ("perf(bigdecimal): serialising a
schedule cost 18x computing it"), and master already carried `762c4301` with the
identical blob for `src/bigdecimal.cppm`. So it was **merged in content and not
in ancestry**: `git merge-base --is-ancestor` said "not merged" and `git branch
-d` refused, correctly, while the work was entirely present.

The empty merge exists to record the ancestry so `3bec86fe` stays reachable and
the branch can be reaped without orphaning it. Confirmed after the fact:
`merge-base --is-ancestor 3bec86fe origin/master` → yes, and both sensen remotes
now list `refs/heads/master` alone.

**Lesson, written down because the standard check pointed the wrong way:**
ancestry and content are different questions. A cherry-picked or rebased branch
is where they diverge, and it is where "safe delete" silently becomes "never
deletable".

## Topology, established before changing anything

Both repositories vendor sensen; **only one of them deploys it.**

| repo | pin before | compiles sensen? | deploys? |
| --- | --- | --- | --- |
| `OptionsAndFuturesCalculator` | `12fe563d` | yes — `sensen_slim` | **yes** — Railway `options-calculator-backend` |
| `mortgage-nest-egg` | `762c4301` | **no** | no — `mfv-web` builds with `vite`, never enters `backend/` |

`mortgage-nest-egg/backend/README.md` states it outright: *"Nothing in this
repository's build compiles them."* One engine serves both products, so "both
websites use the latest sensen" is ONE deployment. MFV's pointer was moved to the
same commit anyway, so the two trees do not describe different libraries — and
that half changes no deployment.

## What the bump actually contains

37 commits, and **not one of them is in the code the engine links.**

- Changed `src/` files: 57. Intersection with the `sensen_slim` module list: **empty**.
- `financial.cppm`, `bigdecimal.cppm`, `options.cppm`, `portfolio.cppm`: **byte-identical** across the range.
- The volume is elsewhere — `speech_dispatch.cppm` +8,049, `speech_backend.cppm` +3,971, `forced_align.cppm` +3,776, plus `src/cuda/` and `agent/`.

So the finance surface both sites call could not have moved, and the gates below
are confirming that prediction rather than discovering the answer.

## Gates

| gate | result |
| --- | --- |
| `scripts/sensen_module_closure.py --check` | only its two documented false positives (`logger.cppm` MISSING, `numa_bind.cpp` EXTRA); **no new entry** |
| `ninja -C backend/build` | rc 0, 178 steps, `calculator_engine` relinked |
| `ninja -C backend/build build_tests` | rc 0, 164 steps (the SGEE targets plain `ninja` skips) |
| `ctest --test-dir backend/build` | **117/117 passed, 0 failed**, 202.49 s |
| `smoke_client … finance` | rc 0 — `pmt(300k,6%,30y) = -1798.651575` = closed form, parity, schedule closure, recast linearity, adversarial + absent-required |
| `smoke_client` full options suite | rc 0 with `PRO_GATE_MODE=off` — bond price/yield inversion, rent-vs-buy closed form, `NPV(IRR) = -0.000000` |
| OFC frontend `npm test` | 24 files / **305 tests** passed |
| OFC frontend `npm run build` | rc 0 — `check-export` OK, `check-indexability` OK (59 pages swept) |
| MFV `npm test` | 92 files / **1075 tests** passed, 6 files + 33 tests skipped |
| MFV lint / tsc / build | 0 / 0 / 0 |

## Two traps hit on the way, both worth keeping

**1. The local engine was serving a deleted binary.** It had been up since
2026-09-18; after the rebuild `/proc/<pid>/exe` read
`.../calculator_engine (deleted)`. `ps` showed the same path and argv and looked
entirely healthy. Smoke-testing it would have measured the PRE-BUMP engine and
proved nothing. This is the `ninja && ctest` stale-binary trap at runtime. Kill
by port (`ss -lptn 'sport = :50051'`) — `pkill -x calculator_engine` matches
nothing, because `comm` truncates to 15 chars (`calculator_engi`).

**2. A Pro-gated smoke run exits 1 on a healthy engine.** `config/.env` carries
no `SMOKE_PRO_LICENCE` / `SMOKE_PRO_BEARER`, so `smoke_client` runs anonymous and
the calendar spread returns `7 Multi-leg strategies are a Pro feature` —
`PERMISSION_DENIED`, the gate working, with `PRO_GATE_MODE=enforce`. Read the
STATUS CODE before the exit code.

Its real cost is what the refusal hides: the **multi-leg compute path never
runs**, and that is exactly the path a sensen change could move. Re-running with
`PRO_GATE_MODE=off` took the suite to rc 0 and exercised it. A gate that refuses
is not the same evidence as arithmetic that is right; the policy's requirement to
exercise the gate in BOTH directions is unmet locally, and stays unmet — the
admit direction needs a credential this box does not carry, and is proven against
production instead.

## Left undone deliberately

- **The Pro-gate admit direction is not exercised locally.** `LICENCE_SIGNING_KEY`
  exists in `config/.env`, so a licence could be minted; it was not, because that
  means handling a signing secret for a result already established against
  production.
- **`QUOTA_POLICY` and `FINANCE_API_KEYS` did not parse** when `config/.env` was
  sourced into the shell — both are JSON values and shell sourcing mangles them,
  so the engine logged `not valid JSON` and left quotas and key auth DISABLED.
  That is an artifact of how the local engine was launched, not a defect, and not
  how the container starts. Quota and auth were therefore not exercised locally.
- **The assistants have no local weights** (`MODEL_PATH` unset), so both degrade
  to submit-only against the shared queue and the `llm` suite is skipped. A
  supported build, as documented.
