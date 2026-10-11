# Session 2026-10-10 — the sensen bump to master, and the stragglers that were not stragglers

## The bump: `8d39843dd` -> `fcaaa3760`, 652 commits

`8d39843dd` was a genuine ancestor of `origin/master`, so this is a fast-forward of the
pointer rather than a divergence — but a 652-commit pointer move is not a cheap bump by
default, and what made it one was measuring the surface rather than the range:

| measurement | result |
| --- | --- |
| `financial.cppm`, `bigdecimal.cppm`, `options.cppm`, `portfolio.cppm` | **byte-identical** across all 652 commits |
| files changed under `src/` | **20** of 512 changed files |
| of those 20, in the `sensen_slim` module list | **one** — `src/parallel.cpp` (plus `parallel.cppm`) |
| `scripts/sensen_module_closure.py --check` | clean, **no new entries** |

So the finance surface both websites call could not have moved, which is the assertion to
make rather than re-deriving it from a 512-file diff.

## WHAT THE BUMP IS ACTUALLY FOR: ten FlatPool fixes

The one slim-listed file that changed is the one that mattered. `src/parallel.cpp`'s
`FlatPool` is rewritten across ten commits in this range, and they are correctness fixes,
not features:

- workers move from **detached threads to `std::jthread` + stop tokens**, with a real
  destructor that requests the stop, advances the epoch so a worker about to park fails
  the value comparison instead of sleeping, and **joins** — explicitly in `~FlatPool`'s
  own body, because members are destroyed in reverse declaration order and leaving the
  join to `workers_` would end `state_`, `done_chunks_`, `sleepers_` and the job
  descriptor while workers still read them;
- a body's exception **on any thread** is caught in `run_chunks` (`fail_job`) and
  rethrown on the caller after the job drains, instead of unwinding a worker into
  `std::terminate`;
- `fork()` from any thread no longer waits on a job (per-process pool generations replace
  the atfork lock), the registry and its fork handler are built at library load so no
  `fork()` lands inside a one-time init, and a forked child abandons the inherited pool;
- worker self-join, the pool-swap race and the dual serialisation are gone, and
  `parallel_for` re-reads the width under the job lock so a concurrent shrink is not
  undone with a stale width.

`lane/fix-flatpool-resize` appears in sensen's merged-branch list, which confirms
independently that this work landed on master rather than sitting on a lane.

This is very likely the **unlocated FlatPool race recorded at the old pin** — the standing
note that races hide above core count and that one was known but not located at HEAD. That
note should not be treated as closed by inspection; it is closed by this pointer move plus
the gate below.

## Gate

A pre-bump baseline was taken first, because a bare green afterwards cannot say whether
anything ever passed — the lesson this repository paid for on the 18->38 scale cutover:

| | baseline (pin `8d39843dd`) | after (`fcaaa3760`) |
| --- | --- | --- |
| build | `build_tests` rc=0, 0 errors | rc=0, 0 errors |
| ctest | **211 tests, 0 failed**, 899.51 s | **211 tests, 0 failed**, 911.92 s |
| skip composition | 7 names | **the same 7 names** |
| `smoke_client ... finance` | — | rc=0, every independent identity satisfied |

Both builds ran `CCACHE_DISABLE=1`, because module interfaces moved (`parallel.cppm` among
them) and ccache hashes a consumer's source text rather than the BMI it compiles against —
the trap that once produced a broken deploy binary from correct source on this tree.

`external/tbqwf` was left **unpopulated** throughout and verified so after the checkout: a
recursive submodule init opens sensen's `-maes -mpclmul` gate, which poisons the one shared
`std.pcm` and makes clang refuse every translation unit in this repository.

**0 gained and 0 lost, against a MEASURED baseline rather than the figure this file
records.** The skip composition is compared by NAME and not by count, because a count
cannot tell an absent broker from a test that quietly stopped being built: both runs skip
exactly `AmqpTransportTests`, `CausalHookMissingFailsByName`, `KafkaTransportTests`,
`NcclCollectiveGate`, `SanitizerOverlayCoversEveryObject`, `TransportConformanceAmqpTests`
and `TransportConformanceKafkaTests` — absent brokers, no GPU, and the sanitizer overlay.

`smoke_client ... finance` was run against a freshly started engine with exactly one
process on `:50051`, after rebuilding `smoke_client` itself: `build_tests` had left it at
its 2026-10-05 mtime while `calculator_engine` relinked, so the smoke gate would have
measured a five-day-old client against a new engine.

Engine banner at the new pin: `SIMD: runtime tier avx512 (avx512f=1 avx512vnni=1
avx512bf16=1 avx2=1 fma3=1), compiled floor x86-64-v3` and `Built by: clang 23.1.2,
libc++ _LIBCPP_VERSION=230102, C++23`.

## The usability lane, merged -- and it was not only usability

`fix/ofc-usability` had sat unmerged with **13 commits**, and the reason it should not have
is that four of them are CORRECTNESS fixes, not polish:

- `22294f8` max profit and max loss are the POSITION's extremes, not the grid's edge
- `6f18a94` refuse a CRYPTO quote instead of pricing the equity of the same name (the
  ES/Eversource root-collision class, on the other asset class)
- `39969d5` switching symbol no longer re-prices the old position, and futures and crypto
  are not priced as equity options
- `f004ade` count an expiration's days at READ time, from the one date source the matrix uses

**Its tip-to-tip diff was misleading and the merge-base diff is the real one.** `git diff
origin/master..HEAD` showed 266 files and +38,907/-17,124, which is not a 13-commit lane --
it is the lane's own work plus master's movement inverted. Against the merge base the
contribution is **85 files, +7,147/-1,410**, 80 frontend and 5 backend. The lane was **41
commits behind**, so master was merged into it first.

Gate, in master after the merge rather than only in the lane:

| | result |
| --- | --- |
| `ctest` (bump + usability + slim) | **211 / 0 failed**, 897.58 s, the same 7 skips by name |
| `smoke_client ... finance` | rc=0 |
| `smoke_client ... ` (options) | rc=0 -- the lane's own path; the assistant suite honestly SKIPS with no local model |
| frontend `npm test` | 36 files / 448 tests |
| frontend `npm run build` + `check-export` | rc=0; 26 article pages >= 600 words and distinct, 35 others (404 and every calculator page) carrying NO ad code, similarity 0.5 median / 0.54 max |
| `scripts/check_start_memory_knobs.sh` | all arms, re-run IN MASTER |

`lane/slim-containers` (2 commits, 4 files) went in the same pass: Envoy's worker cap and
the allocator knobs for the engine and the gateway, with its own gate script -- which
refuses `abc`, `0`, `-1`, `2.5`, ` 3` and `4 ` rather than defaulting them, the strict-switch
rule this tree applies to every operator variable.

## The straggler count was wrong twice, and both errors had one cause

Reported as "30 remote sensen lane branches, each 8-61 commits unmerged". Re-measured, the
local view said **68** branches of which **37** had zero unmerged commits -- and deleting
those 37 failed with `remote ref does not exist` on BOTH remotes, because
`refs/remotes/origin/*` is a LOCAL CACHE and `git for-each-ref` reads the cache while
`git ls-remote` asks the server. A branch someone else deleted stays in the cache until a
prune. After `git remote prune` (**38 stale refs on origin, 5 on gitea**) the real figure is
**30 lane branches, 0 of them merged** -- so the original number was right and both
corrections were artifacts of dead local state.

Same family as this repository's own rule that a push's output is not proof a ref persisted:
only `ls-remote` is. It applies to counting refs as well as to pushing them.

Also cleared: an **orphaned `calculator_engine`** from the deleted `lane-mfva` worktree,
running **3 days 16 hours** and holding port 50877, killed by PID (never by name, on a shared
box). Its 6.2 GB scratch directory was **left alone deliberately** -- it holds
`mortgage-encoder.gguf`, `strategy-encoder.gguf` and three earlier encoder revisions, and a
quantised GGUF on this host may be its only copy. The two `sensen-gateway` processes running
since 2026-10-06 were also left alone: one is `admin reap-loop`, which is the documented
maintainer mechanism for the PostgreSQL tier, not debris.
