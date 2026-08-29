# Session log — 2026-08-29 — clang 22 → clang 23, both halves

@author Olumuyiwa Oluwasanmi

Evaluated LLVM 23.1.0 (released four days earlier), moved both deploy images and
the local development toolchain to it, and fixed the three things that broke.

Full write-ups: `docs/technical/TOOLCHAIN_CLANG23_MIGRATION.md` and
`docs/technical/LLAMACPP_LIBCXX23.md`.

## Result

| | |
| --- | --- |
| local toolchain | clang 23.1.0 at `/usr/local`, **ctest 103/103** |
| deploy images | `ARG LLVM_VERSION=23`, container gate green, in-image smoke suite passes |
| production | deployment `f0657305` then `9a9c99c5`, 4 × `model is LOADED`, 0 WARN/ERROR |
| live probe | `probe_finance_service.py` **34/34** |

## Why

clang-tidy. `SKIP_CLANG_TIDY` has been forced ON "due to c++23 module scanning
bugs", so this repo has had no lint on its own C++ for as long as it has used
modules. Measured on one module, each compiler with its own `std.pcm`:
**11,176 diagnostics → 5**. clang-tidy 23 analyses a real 2,400-line module of
ours and returns 23 actionable findings.

Stated limit, because it would otherwise be over-claimed: this does **not**
re-enable `CXX_CLANG_TIDY`. **0 of our `.cppm` files appear in
`compile_commands.json`** in either build — CMake emits no entries for
`FILE_SET CXX_MODULES` sources. Generator limitation, unchanged by clang 23.

## Three failures, and what each was really about

**1. libc++ located by guessing absolute paths** (SGEE and sensen). Both fell
through to a hardcoded `/usr/local/lib/x86_64-unknown-linux-gnu` — a different
compiler's runtime — and failed with `undefined reference to vtable for
std::__bad_variant_access_with_msg`, which reads as a corrupt standard library
rather than as two of them. Now `-print-file-name`. This is what
`cpp_details.txt` rule 114 already required.

**2. A driver "auto-registration" that was link-order luck** (SGEE). `import`
creates no link-time reference, so the archive member was extracted only while
some other symbol in it happened to be needed. Measured: `graph_tests` carries
**15** `Neo4jConnection` symbols under clang 22 and **0** under clang 23, and
both object files carry `.init_array` — archive-member selection, not codegen.
Fixed with an exported `register_neo4j()`.

**3. Vendored llama.cpp did not compile against libc++ 23.** 221 errors
deduplicating to 13 sites in 7 files — removed transitive includes. Fixed with a
seven-line patch applied via `PATCH_COMMAND`, deliberately not a `GIT_TAG` bump:
llama.cpp is here to be an independent control for the parity probes, so moving
the pin is changing the reference.

None of the three was a clang 23 defect. A second compiler is a differential
test, and that is what it bought.

## The local overlay trap, which cost the most time

`/usr/local` holds a from-source clang 22 **and** cmake, ninja, ctest, TBB, Z3 —
so it could not be wiped, and the LLVM 23 tarball was overlaid onto it. The
first rebuild produced **700+ errors, all inside libc++ itself**:

```
/usr/local/include/c++/v1/cctype:45:8: error: "If libc++ starts defining
  <ctype.h>, the __has_include check should move to libc++'s <ctype.h>"
```

libc++ 22 shipped `ctype.h`, `inttypes.h`, `float.h` and `fenv.h`; libc++ 23
removed them. An overlay adds and overwrites, never deletes, so four dead
headers shadowed the C library's. **1,701 files locally against 1,688 upstream.**
Fixed by replacing `include/c++/v1` wholesale.

**An overlay install of a compiler is not equivalent to a clean one, and the
difference surfaces as errors INSIDE the new standard library.** Diff the file
lists.

Rollback is a file, not a procedure: `~/usr-local-llvm22-backup.tar.zst` (5.1 GB).

## Self-inflicted, recorded so it is not repeated

`rm -rf backend/build` also removed `_deps/grpc-src`, which the other two build
directories referenced via `FETCHCONTENT_SOURCE_DIR_*`, forcing a full gRPC
re-clone and rebuild. A shared FetchContent source makes one build tree
load-bearing for the others.

## Deliberately not done

- **sensen's submodule pointer is not bumped.** Master moved 40 commits of
  unrelated GPU/RL work; bundling that with a toolchain change would make any
  failure unattributable. The libc++ fix is upstream at `a76d589b` and is not
  load-bearing here.
- **llama.cpp's sources are not restyled** to `cpp_details.txt`'s house rules.
  That would erode the independence the dependency exists for and would have to
  be redone on every bump.
