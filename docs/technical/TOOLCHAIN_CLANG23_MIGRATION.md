# Moving the toolchain from clang 22 to clang 23

@author Olumuyiwa Oluwasanmi

2026-08-29. LLVM 23.1.0 was released 2026-08-25, four days earlier.

Both halves moved: the two deploy images (`ARG LLVM_VERSION=23`) and the local
development toolchain (`/usr/local`). This document is the evaluation, the
decisions, the three things that broke, and what each one taught.

---

## 1. Why it was worth doing

**clang-tidy is the reason.** `backend/CMakeLists.txt` forces
`SKIP_CLANG_TIDY ON` with the comment *"due to c++23 module scanning bugs"*, so
this repository has had no lint at all on its own C++ for as long as it has used
modules.

Measured A/B on one module, each compiler with its **own** `std.pcm`:

| | diagnostics on the same file |
| --- | --- |
| clang-tidy 22 | **11,176** — it walks into libc++'s own `std.cppm` internals |
| clang-tidy 23 | **5**, with 4 more correctly suppressed as non-user code |

clang-tidy 23 also analyses a real 2,400-line module of ours
(`backend/src/modules/mortgage_verification.cppm`) and returns 23 actionable
diagnostics with no crash and no flood.

**The first A/B was invalid, and the way it failed is worth keeping.** It scored
clang-tidy 22 against clang 23's `std.pcm` and produced four confident-looking
errors — *"module file 'std.pcm' uses a newer format that cannot be read"*
followed by three `use of undeclared identifier 'std'`. A BMI is version-locked.
A cross-version comparison must build one BMI per compiler or it is measuring
the harness, which is the third time this repository has caught a score
describing its own instrument.

**State the limit as loudly as the win: this does not re-enable
`CXX_CLANG_TIDY`.** Measured, **0 of our `.cppm` files appear in
`compile_commands.json`** in either build, because CMake emits no database
entries for `FILE_SET CXX_MODULES` sources. That is a generator limitation, not
a clang-tidy one, and clang 23 does not change it. A wrapper that passes flags
directly is what works today.

Secondary: `[[msvc::forceinline]]` / `[[msvc::forceinline_calls]]` arrive as
aliases for `[[clang::always_inline]]`, which matters for the Windows branches
sensen carries.

---

## 2. Availability, checked before anything was changed

`apt.llvm.org` ships the complete set for noble — `clang-23`, `clang-tidy-23`,
`clangd-23`, `clang-format-23`, `clang-tools-23`, `lld-23`, `libc++-23-dev`,
`libc++abi-23-dev`, `llvm-23-dev` — and `llvm.sh` has `LLVM_VERSION_PATTERNS[23]`.

The package version strings are the same *shape* as the 22 we already shipped:

```
clang-22: 1:22.1.8~++20260714014902+ca7933e47d3a-1~exp1~...
clang-23: 1:23.1.0~++20260818083557+55feb0a3b6b7-1~exp1~...
```

Both are snapshot-form with `exp1`. That answers "is 23 packaged less
seriously than 22?" — it is packaged identically.

---

## 3. Gates, in the order they were run

Local first, because it is cheap; the container last, because it is what
deploys.

| gate | result |
| --- | --- |
| engine builds, clang 23 | **0 errors** |
| `ctest`, clang 23 | **103/103** — same count, same tests as clang 22 |
| errors in our code / sensen / SGEE / gRPC / protobuf | **0** |
| `podman build --build-arg LLVM_VERSION=23` | **success** |
| llvm-23 vs llvm-22 references in that image build | **378 vs 0** — no silent fallback |
| in-image `smoke_client … finance` | full suite, `pmt(300k,6%,30y) = -1798.651575` against the closed form |
| production deploy `f0657305` | SUCCESS, 4 × `model is LOADED`, 0 WARN, 0 ERROR |
| live `probe_finance_service.py` | **34/34** |

The in-image smoke run is the one that matters: a green healthcheck says a
container started, and only an answer that satisfies an independent identity
says the compiler produced a correct binary.

---

## 4. What broke, and what each one taught

### 4.1 libc++ located by guessing absolute paths (SGEE and sensen)

Both walked a fixed list of directories and fell through to a hardcoded
`/usr/local/lib/x86_64-unknown-linux-gnu` — a **different compiler's** runtime.
A relocatable LLVM release installs its runtimes at
`<root>/lib/<target-triple>/`, which no entry in either list matched.

Objects compiled against libc++ 23 headers then linked against libc++ 22:

```
undefined reference to `vtable for std::__bad_variant_access_with_msg'
```

That symbol is a libc++ internal introduced in 23. The message **reads as a
corrupt standard library rather than as two of them**, which is what makes it
expensive. Configure printed a path and no warning, and the *engine* linked
fine — only SGEE pins a path, so it surfaced solely in SGEE's own tests.

Fixed in both by asking the compiler: `-print-file-name=libc++.so`. The guesses
remain as a fallback and are no longer reached on either toolchain. This is what
`config/cpp_details.txt` rule 114 already required — *"absolute system paths are
NEVER used because cloud boxes may have different LLVM versions installed,
causing silent partial builds"*. The rule was right; these two files were the
exception nobody had tripped.

**Lesson: a guess that happens to hit is indistinguishable from the right answer
until it misses.**

### 4.2 A driver "auto-registration" that was link-order luck (SGEE)

`GraphTests` failed under clang 23 and passed under clang 22 on identical
source. The assertion was `GraphDriverRegistry::has("neo4j")`.

`neo4j_connection.cppm` registers the driver from a namespace-scope object's
constructor. No consumer names a symbol in that translation unit — **`import`
makes declarations visible and creates no link-time reference** — so when the
file is compiled into a static archive the linker has no reason to extract the
member, and an initializer in an unextracted member never runs. It worked only
while some *other* symbol there happened to be needed.

Measured, which is what turned a mystery into a diagnosis:

| | `Neo4jConnection` symbols in `graph_tests` | `has("neo4j")` |
| --- | --- | --- |
| clang 22 | **15** | true |
| clang 23 | **0** | false |

And both object files carry `.init_array`, so **both compilers emit the
initializer** — this is archive-member selection, not codegen, and clang 23
merely stopped supplying the accident.

Fixed with an exported `register_neo4j()`, mirroring `db_sql_ext`'s existing
`register_postgresql`. A named function is a real symbol reference, which is the
only form that does not depend on link order. `[[gnu::used]]` would **not** have
fixed it: it stops the compiler discarding the object, and the object was never
the problem.

**Lesson: a second compiler is a differential test. Neither of these was a
clang 23 defect; both were ours, and both had been latent for as long as the
code existed.**

### 4.3 Vendored llama.cpp did not compile against libc++ 23

221 errors, all in `_deps/llamacpp-src`, deduplicating to thirteen sites in
seven files: libc++ 23 removed transitive includes, so `getenv`, `atoi`,
`strtol` and `std::max` stopped being reachable.

Fixed with a seven-line include patch applied via `PATCH_COMMAND`, deliberately
**not** a `GIT_TAG` bump. Full rationale and method:
[`LLAMACPP_LIBCXX23.md`](LLAMACPP_LIBCXX23.md).

---

## 5. Moving the LOCAL toolchain, and the trap in an overlay install

The images moved first; local followed so that development and deployment stop
diverging — which is what `backend/Dockerfile`'s own comment asks for.

`/usr/local` already held a from-source clang 22 **plus** cmake, ninja, ctest,
dnf, kpatch, TBB and Z3 — 185 binaries of which 157 are LLVM. So it could not be
wiped; the LLVM 23.1.0 release tarball was **overlaid** onto it, which writes
only its own files.

Backed up first, because a from-source LLVM is hours to rebuild and a rollback
has to be a file rather than a procedure:

```
~/usr-local-llvm22-backup.tar.zst   5.1 GB
tar -C /usr/local --zstd -xf ~/usr-local-llvm22-backup.tar.zst   # to roll back
```

### The overlay left four dead headers, and they read as a broken release

The first rebuild produced **700+ errors, all inside libc++ itself**:

```
/usr/local/include/c++/v1/cctype:45:8: error: "If libc++ starts defining
  <ctype.h>, the __has_include check should move to libc++'s <ctype.h>"
```

libc++ 22 shipped four C-compatibility headers — `ctype.h`, `inttypes.h`,
`float.h`, `fenv.h` — that **libc++ 23 removed**. An overlay adds and overwrites;
it never deletes. Those four sat there shadowing the C library's headers, and
libc++ 23's own `<cctype>` tripped its guard. Counted: **1,701 files locally
against 1,688 upstream** — thirteen strays, four of them fatal.

Fixed by replacing `include/c++/v1` **wholesale** instead of merging into it, and
`share/libc++` and `lib/x86_64-unknown-linux-gnu` were diffed the same way (both
clean).

**Lesson: an overlay install of a compiler is not equivalent to a clean one, and
the difference surfaces as errors INSIDE the new standard library — which reads
as a broken release rather than as your own leftovers. Diff the file lists.**

### Pass the real compiler path, never the ccache shim

`which clang++` resolves to `/usr/lib64/ccache/clang++`. CMake recording that as
`CMAKE_CXX_COMPILER` sends sensen's
`dirname(dirname(compiler))/share/libc++/v1/std.cppm` walk to
`/usr/lib64/share/...`, which does not exist. This is the same failure
`backend/Dockerfile` already documents for `/usr/bin/clang++`.

```bash
cmake -G Ninja -B backend/build -S backend \
  -DCMAKE_C_COMPILER=/usr/local/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/local/bin/clang++ \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

Same caching, working walk.

### One self-inflicted cost, recorded so it is not repeated

`rm -rf backend/build` also removed `_deps/grpc-src` and `_deps/httplib-src`,
which the other two build directories referenced via
`FETCHCONTENT_SOURCE_DIR_*`. That forced a full gRPC re-clone and rebuild.
**A shared FetchContent source directory makes one build tree load-bearing for
the others**; delete only the build outputs, or repoint the overrides first.

---

## 6. Files changed

| file | change |
| --- | --- |
| `backend/Dockerfile` | `ARG LLVM_VERSION=23`; version references and the LLVM-18 skew arithmetic corrected (five majors, not one) |
| `backend/Dockerfile.queue-node` | same |
| `config/cpp_details.txt` | rules 50/60 name `clang++-23` |
| `config/update_policy.txt` | canonical toolchain is clang 23, with the ccache-shim guidance |
| `backend/CMakeLists.txt` | `LLAMACPP_CXX_STANDARD` knob; `PATCH_COMMAND` for the llama.cpp patch |
| `backend/patches/llamacpp-b6963-libcxx23-includes.patch` | new — seven `#include` lines |
| `backend/external/SGEE` | libc++ from `-print-file-name`; exported `register_neo4j()`; test renamed |
| `backend/sensen` | libc++ from `-print-file-name` (upstream `a76d589b`, pointer deliberately not bumped) |

**sensen's submodule pointer is NOT bumped.** Its master moved 40 commits of
unrelated GPU/RL work since this tree's pin, and bundling that with a toolchain
change would make any failure unattributable. The fix is also not load-bearing
here — the clang 23 engine linked at `rc=0` before it.

---

## 7. Doing a toolchain move again

1. **Check packaging availability before touching anything** — and compare the
   version-string *shape* against the release you already trust.
2. **Build in a separate directory.** Never disturb the working toolchain until
   the new one has passed a full `ctest`.
3. **Classify every error by whose code it is in.** All-third-party means the
   toolchain is fine.
4. **Gate on the container, not the local build**, and inside the container run
   something that checks an *answer*, not just that a process started.
5. **Expect the new compiler to find your latent bugs.** Budget for it; that is
   the value, not the cost.
6. **Prefer a clean install to an overlay.** If you must overlay, diff the file
   lists afterwards and delete what the new release dropped.
7. **Keep a file-based rollback** for anything that takes hours to rebuild.
