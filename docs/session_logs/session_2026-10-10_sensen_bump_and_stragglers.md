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
