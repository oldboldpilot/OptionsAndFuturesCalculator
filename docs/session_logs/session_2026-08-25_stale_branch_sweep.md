# Stale-branch sweep and the sensen resident-memory merge

**Date:** 2026-08-25
**Author:** Olumuyiwa Oluwasanmi

## What was asked

Confirm there are no stale branches anywhere in the tree and merge whatever is
outstanding, following `config/update_policy.txt` (and, for the vendored
library, `backend/sensen/config/update_policy.txt`).

## What the sweep found

The top-level repository was already clean — one branch, no worktrees, nothing
uncommitted, `master` identical on `origin` and `gitea` at `5b87966`. The stale
state was one level down.

| repository | state before | action |
| --- | --- | --- |
| `OptionsAndFuturesCalculator` | clean, both remotes at `5b87966` | pointer bump + docs |
| `backend/sensen` | HEAD on `perf/quantized-resident-embedding`, 4 commits off master | **merged to master, branch deleted** |
| `backend/external/SGEE` | clean, both remotes at `28537cc5` | none |
| `backend/cpp23-logger` | pinned 3 behind master; stale `origin/bmi-compatibility-flags` | **flagged, not merged — see below** |
| `backend/nanobind` | third-party upstream, 103 behind | none — not ours |

An early reading suggested sensen's two remotes had diverged (`origin/master`
`5e4bb51f` vs `gitea/master` `06a5557f`). They had not; both refs were stale
locally and `git fetch --all` converged them on `1069a49f`. Fetch before
concluding two remotes disagree.

## The merge

`perf/quantized-resident-embedding` carried the four commits that took boot RSS
from 5,420 MB to 2,942 MB with both assistants loaded, and had been serving
production since 2026-08-21 from the branch tip:

    604d1d1c  hold token_embd once, quantized                 -601 MB per model
    78afb203  stop zero-filling F32 buffers never read
    65adac57  one shared cos/sin table, not one per layer      -612 MB per model
    dc51084b  honour SENSEN_QKV_FUSION at build, not at use    -239 MB

Merged as `7aa1d94e` against master's six intervening commits. Pushed to
`origin` and `gitea`; branch deleted on both after confirming `a5e791b2` is an
ancestor of master, so the parent's existing pin stayed valid throughout.

## Gate

- `ninja -C backend/build build_tests` — exit 0, `[295/295]`
- `ctest --test-dir backend/build` — **100/100 passed, 0 failed**, 201.21 s
- `ninja -C backend/build` — exit 0, `calculator_engine` relinked
- libpq isolation invariant re-verified on the built objects:
  `calculator_service.cpp.o` 0 `pg::` symbols, `strategy_store.cpp.o` 13,
  `pg.cppm.o` 18 `PQ`

Only third-party warnings (abseil `float_denorm_style` deprecation via gRPC).

## Three things worth carrying forward

1. **A clean merge across an overlap is not evidence of correctness.** Master's
   `1069a49f` and `e2056a3c` changed attention; `65adac57` and `dc51084b`
   changed attention. Git merged them without conflict because they touched
   different lines. ctest is what settled it.
2. **`ninja build_tests` does not build `calculator_engine`.** Its sweep matches
   `^test_` only. The sensen objects relinked at 07:35 while the engine binary
   still dated from the previous evening — a green ctest beside a stale deploy
   artefact. Plain `ninja` is a separate, required step.
3. **Module symbols carry their module in the mangled name.**
   `nm -C | grep 'RotaryEmbedding::shared'` returned 0 for a symbol that is
   present as `RotaryEmbedding@sensen.rotary_embedding::shared`. The false
   negative looks exactly like a failed link.

## Left undone, deliberately

`backend/cpp23-logger`'s `origin/bmi-compatibility-flags` (`e27ba16`, December
2025) is unmerged and was NOT merged. It rewrites `CMakeLists.txt` to match BMI
flags via `UNIVERSAL_MODULE_FLAGS`, which predates this tree's August 2026 move
to a single shared `std.pcm` bound with `-fmodule-file=std=`. Master's own
`59d498c` ("replace hardcoded -fprebuilt-module-path with derived paths")
addresses the same problem from the current design. Merging it would reinstate a
superseded approach, so it is a deletion candidate rather than a merge
candidate — that call belongs to the owner.

`backend/nanobind` is upstream `wjakob/nanobind`, pinned at `v2.14.0-6`. Its
eleven branches are not ours and are out of this policy's scope.
