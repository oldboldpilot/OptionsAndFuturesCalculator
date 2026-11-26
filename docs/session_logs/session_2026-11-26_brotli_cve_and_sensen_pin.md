# 2026-11-26 — Brotli CVE-2026-111497, and a 280-commit sensen pin verified but not moved

## Brotli (shipped)

CVE-2026-111497 is an out-of-bounds write in the Brotli decoder, Ubuntu priority high
(USN-8956-1/-2, USN-8958-1), fixed upstream in brotli 1.0.10/1.1.1/1.2.1. The full reasoning and
every measurement is in docs/ENGINEERING_NOTES.md under "Brotli CVE-2026-111497: three surfaces,
and only one of them apt can fix". What changed in the tree:

- A version-FLOOR gate after every apt install that brings in a brotli package —
  backend/Dockerfile (builder and runtime), backend/Dockerfile.queue-node (builder and runtime),
  deploy/gateway/Dockerfile. Floor `1.1.0-2ubuntu0.1`. It counts the packages it compared, so it
  cannot pass vacuously.
- Two comments corrected where measurement contradicted them: backend/envoy.yaml claimed the Envoy
  binary links libbrotlidec and that libbrotli1 is needed for Envoy to load (both false — Envoy
  carries its own brotli statically and the upstream image has no libbrotli package at all), and
  backend/CMakeLists.txt still said Ubuntu 24.04 ships only the affected 1.1.0.

Controls, all run rather than reasoned about:

| control | result |
| --- | --- |
| gate with a patched `libbrotli1` | build rc=0, "1 package(s) at or above 1.1.0-2ubuntu0.1" |
| gate with no brotli package | FATAL "would pass vacuously" |
| gate with `libbrotli1=1.1.0-2build2` forced | FATAL naming the CVE and both versions |
| brotli decompressor accepted by envoy v1.33 | `configuration OK` |
| a real-but-unregistered type, same validation | "Didn't find a registered implementation" |
| this repository's envoy.yaml on v1.33/v1.36/v1.39/v1.40 | `OK` on all four |
| `ldd` on `calculator_engine` and `smoke_client` | no libbrotli in either NEEDED closure |

Open, and deliberately not done here: moving the Envoy pin off 1.33 (a branch with no fixed
release) to 1.36.12+/1.39.3/1.40.0. The config validates unchanged on all of them, so the edit is
one line; the verification is a deploy, which is why it is a separate decision.

## sensen pin (verified, NOT moved)

New sensen master `26d90bb36` is FAIL: 19 on its own slim-closure checker (the growth history is
4 → 7 → 13 → 19), so the current pin `2b842edd6` remains the newest green commit. The 19 are two
events: an llm_pipeline implementation consolidation, and seven translation units added to the
source tree and never registered in the slim group registry. A +10/−2 registry fix takes the
checker to OK, and with it the whole bump verifies CPU-only in this superproject:
scripts/sensen_module_closure.py --check clean at 163 modules / 212 files (was 204), the full
engine built in 5504 steps with 0 failures, and `ctest` 217/217 passing.

Two defects surfaced on the way, neither in this repository's modules:

- A real regression in an SGEE test, found as SIGILL with no output. The faulting instruction was
  `ud2` — a compiler trap, not an unsupported instruction, which is what redirected the diagnosis.
  The new sensen enables _LIBCPP_HARDENING_MODE_FAST build-wide, which turns the precondition of
  `std::expected::error()` (`!has_value()`) into a trap; the test called it as an argument to an
  ordinary function guarded by `has_value()`, and an ordinary call evaluates every argument. A
  12-line standalone program reproduced it: rc=0 without the flag, SIGILL with it. Sweeping the
  class found exactly two sites, both in one file, with 37 short-circuit sites correctly excluded —
  `&&` does not shield this, `||` and `?:` do.
- A ctest failure that was the harness, not the pin: the TBB sanitizer-reconfigure test globs
  `_deps/*-src` under the parent build directory, and the scratch build had FETCHCONTENT_BASE_DIR
  redirected to share a dependency tree. One symlink, nothing else changed, and it passed.

Neither fix belongs in this repository: the registry fix belongs in sensen, the two-line guard in
SGEE. Both working trees were restored to their pins and nothing was committed here.
