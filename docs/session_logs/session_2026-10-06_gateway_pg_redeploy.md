# Session 2026-10-06 — the TIME_WAIT fix reaches the rig, and Rule 3 learns an idiom

Picks up `backend/sensen/docs/session_logs/session_2026-10-06_gateway_pg_cutover.md`,
whose open item 1 was "redeploy `64c3e1f5f`". That is done and proven.

## What was deployed

| | before | after |
| --- | --- | --- |
| sensen | `dd1a12547` | `a041789af` |
| tbqwf | `539022e78` | `474628e65` |

The tbqwf pin is the whole point: `64c3e1f5f` bumps it because TBQWF passed
`SO_REUSEADDR | SO_REUSEPORT` as ONE option NAME (`2|15 == 15`), which is what
restart-looped the gateway twenty times on "port in use" while nothing listened.

Measured by `deploy/gateway/redeploy.sh`:

```
preflight ok: sensen=a041789af tbqwf=474628e65
regression rc=0            20 passed, 0 failed
reaper ok: tick 1 outcome=not_due ... orgs_without_retention=0
restart ok: ready in 11s   (budget 20s)
restart count: 0
regression after restart rc=0  20 passed, 0 failed
```

## A COLD /readyz CANNOT SEE THIS DEFECT, which is why the script's gate is a restart

A container that has served no traffic holds no socket in TIME_WAIT, so it
answers `/readyz` on the broken image exactly as it does on the fixed one, and
`ss -ltn` being clean is not "port bindable" either. The gate is therefore
deploy -> regression -> restart IMMEDIATELY -> ready inside a budget ->
regression again. 20 s discriminates: comfortably above a healthy restart and
comfortably below the <=60 s stall.

## `gw_cutover4.sh` COULD NOT BE REUSED, and reaching for it would have been wrong

It is a one-way SQLite -> PostgreSQL migration: step 5 stops
`sensen-gw-reaper-authz` and its failure path starts that container back up.
That container is already exited and the PostgreSQL store is now the system of
record, so a "rollback" there would serve older data and call it success.
`deploy/gateway/redeploy.sh` is the other operation — re-image and replace —
and it deliberately does NOT roll back.

It also lived only in `~/.claude/jobs/606f4f9d/tmp/`, an agent job's scratch
directory, so the procedure for deploying this tier had no durable home (P4).
The regression key had the same problem and is now
`~/.config/sensen-gateway/regress.key` (0600), beside the DSNs.

## NEW FINDING, not fixed: the gateway does not honour SIGTERM

```
StopSignal SIGTERM failed to stop container sensen-gw-pg in 10 seconds,
resorting to SIGKILL
```

So roughly 10 of those 11 seconds are podman's stop timeout and the real
bind-and-ready is about 1 s — and shutdown is never graceful, which drops
in-flight requests. The pin bump does not touch this. Untriaged.

## Rule 3 flagged the private-constructor factory idiom, and the ROOT POLICY says fix the rule

`scripts/code_policy_check.sh` was red on five sites before anything here was
touched, all of the shape `std::unique_ptr<T>(new T(...))` in
`encoder_assistant.cpp` and `llq_weight_store.cppm`. Verified rather than
assumed: `LlqQ8Source`, `LlqBf16Source` and `LoadScope` all declare their
constructor `private`. `make_unique`/`make_shared` REQUIRE public construction,
so that expression is the only way to hand out an owning smart pointer to such
a type, and ownership transfers inside it — no owning raw pointer ever exists
as a variable, which is what the rule is about.

The alternative was a pass-key tag struct on five classes so a checker would
stop objecting. `config/cpp_details.txt` names that as a defect in its own
right ("Code bloat is a defect") and P8 forbids it, and the same file says
"where a numbered rule and a principle seem to conflict, the principle wins and
the rule is the thing to fix". P2 is the principle: a private constructor plus
a static factory IS information hiding.

**It needs the previous line, which a line-based grep cannot see.** Three sites
write the idiom on one line; two break after the opening paren, leaving
`new LlqBf16Source(...)` alone with no smart pointer in sight. Each candidate
is joined with its predecessor and whitespace-stripped, so both layouts collapse
to `shared_ptr<constLlqBf16Source>(newLlqBf16Source(`.

Narrow by construction, and mutation-checked three ways:

| arm | result |
| --- | --- |
| the five factory sites | PASS — gate green |
| `auto* p = new Widget();` | STILL FAILS |
| `foo(std::unique_ptr<A>(x), new Widget())` | STILL FAILS |

The near-miss still fails because what follows `>(` there is `x`, not `new`.

## Traps this session paid for

- **`ninja … | tail -40; echo rc=$?` reports TAIL's status.** The first rebuild
  FAILED and was recorded as `rc=0`; only the binary's unchanged mtime gave it
  away. Redirect to a file and read ninja's own rc.
- **sensen master does not configure on a WARM build directory.** Its own
  `CMakeLists.txt` runs clang-tidy at CONFIGURE time over 380 files and
  `FATAL_ERROR`s. Note it prints `Enable clang-tidy: OFF` and runs anyway — the
  pre-build validation is a different code path from `CXX_CLANG_TIDY`. The lever
  is `SKIP_CLANG_TIDY=1` in the ENVIRONMENT. Not this project's debt.
- **`find … -print -quit && echo STALE` always fires.** find's exit status says
  whether find RAN, not whether it MATCHED. Capture the output and test it
  empty — which `redeploy.sh` does, though the ad-hoc check that found this did
  not.

## Left alone deliberately

`backend/sensen` is checked out at `65dde8d32` on
`lane/artifact-budget-and-attestation-skew` while sensen master is `a041789af`
(an ancestor relationship — the checkout is simply behind). The pointer is NOT
committed here: an OFC pin bump has its own documented gate (module closure
check, ctest, `smoke_client`), and none of it was run. The gateway that was
redeployed is built from the `sensen-gwpg` worktree, not from this pin.
