# 2026-10-07 — #81 step 2: the build recipe moves into sensen

The mortgage assistant is moving into mortgage-nest-egg (#81). Its new service must build from sensen without copying this
repository's hand-kept `sensen_slim` list or its gRPC block, so both now live in sensen and this engine consumes them.

## What moved

- **Into sensen** (`7887a260`; code in `b1c0734d`): `cmake/SensenSlim.cmake` (the module list as eight named groups --
  FOUNDATION, NUMERIC, MATH, RUNTIME, ENCODER, LOGIC, FINANCE, LLM -- and `sensen_add_slim_library`, which carries every wiring step
  the old target had), `cmake/SensenGrpc.cmake` (`sensen_fetch_grpc()`: pin, patch, options) with the clang-20 patch under
  `cmake/patches/`, `tools/sensen_slim_closure.py`, `tools/check_slim_encoder_only.sh`, an opt-in probe under `tests/slim/`.
- **Out of this repository:** the ~350-line `sensen_slim` definition and the gRPC block in `backend/CMakeLists.txt`,
  `backend/patches/grpc-1.62-clang20-conformance.patch` (the llama.cpp patch stays), and the checker's body --
  `scripts/sensen_module_closure.py` is now a thin call that points sensen's checker at `backend/src` with every group.
- **What stays here, on purpose:** the canonical compile flags, the option block that embeds sensen (SGEE off, no tests, no
  benchmarks), and the one ordering rule that depends on this file -- `sensen_use_shared_std_module()` after
  `add_subdirectory(sensen)` and before `sensen_slim`. The function that makes the library now REFUSES to run without it.

## Gate

- **Source set:** the sorted objects of `sensen_slim` are identical before and after (129 against 129, empty diff), and the 258
  build commands for them are byte-identical. Only the archive member order in `libsensen_slim.a` differs, because the sources are
  now listed by group.
- **ctest 218**: 208 passed, 8 skipped, 2 not run (CausalityBenchGate, CausalityBenchGateCanFail), 0 failed -- per-test identical to
  the baseline taken first on the old tree (the 218-line status lists diff empty).
- **Parity:** tokenizer 2472/2472, lexer 2472/2472 (10,687 literals), reconstruct 2169/2169, each byte-identical to the baseline.
- **Closure check** (`python3 scripts/sensen_module_closure.py --check`): OK, with four standing items now DECLARED in the registry
  instead of reported: the CPU-excluded `text_encoder_cuda.cpp` / `text_encoder_triton.cpp`, the C-ABI `numa_bind.cpp`, and
  `special/foundation.cppm`. The old checker reported `logger.cppm` and `logger_io.cpp` as MISSING because it matched on basename;
  the new one matches by path and does not. `special/foundation.cppm` is a real dead entry the old regex could not see (imported by
  nothing since the multiprecision bump); it is kept so the move leaves the source set unchanged and is declared as retained.
- **ToolchainBMIGuard:** unchanged. `scripts/check_bmi_guard_identity.sh` reports sensen's and SGEE's copies byte-identical to
  `backend/cmake/`'s, and sensen includes it unconditionally at its top level, so a consumer that embeds sensen gets the sweep.
- **Second consumer:** a scratch project shaped like the new service (its own flags, `sensen_fetch_grpc()`, sensen embedded,
  `GROUPS ENCODER`) built 63 files against this engine's 129, linked with zero `llm_pipeline`/`qwen38`/`sgee`/`pg::` symbols
  (the full archive: 364 and 219), and parsed "500000 at 7.25% for 30 years" to `ComputePayment` with a real encoder GGUF.

## Not done here

- CLAUDE.md still describes the slim list and the closure script as living in this repository ("sensen builds without CUDA, and the
  slim list is hand-maintained", the gRPC section); the docs step rewrites it with the later moves.
- The Dockerfile builder stage is plan step 4. The gRPC macro could not be exercised against a PRISTINE fetch here (this build
  directory points `FETCHCONTENT_SOURCE_DIR_GRPC` at an already-patched checkout), so the patch was checked directly: it applies to a
  pristine v1.62.0 and passes its own `--reverse --check` the second time.
- `EncoderAssistant::fromGguf` throws on an unreadable path where its signature promises an error value; moved as it was, noted in
  sensen's changelog for the service that calls it.
