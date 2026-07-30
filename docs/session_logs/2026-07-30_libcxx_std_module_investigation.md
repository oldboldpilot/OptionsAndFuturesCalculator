# Session Log: 2026-07-30 — `import std` / libc++ Investigation (Not Adopted)

@author Olumuyiwa Oluwasanmi

An attempt to move the backend off header-std (`sensen_std.h` / `sgee_std.hpp`)
onto libc++ with a real std module, so both submodules would compile as written.
**The attempt was not adopted.** `backend/CMakeLists.txt` is unchanged; the
investigation is recorded because it establishes exactly what blocks the change
and disposes of several wrong explanations, including one this log previously
carried.

## Why This Was Attempted

`backend/CMakeLists.txt:3` sets `SENSEN_NO_IMPORT_STD` for the whole project,
which is what forces the header-std path everywhere and, in turn, is why SGEE
was edited in place to include a `sgee_std.hpp` shim. The question was whether
that shim was ever necessary. It is a fair question: sensen's own default on
Linux + Clang is `import std;`, and SGEE's documented build is `import std` +
libc++.

## `import std` Itself Works Here

Established with a minimal twelve-line project before touching the backend:
clang 22.1.0 + libc++ + a CMake-managed std module compiles and runs
`import std;` on this machine. So nothing about the platform prevents it. That
minimal test is the diagnostic worth keeping — if `import std` compiles there,
every remaining failure is integration, not capability.

## Four Real Faults Found And Fixed Along The Way

Each of these presents as "`import std` doesn't work on this platform", which is
why the header shim looked like the only option.

1. **Wrong experimental-gate UUID.** `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` is
   keyed to the CMake version, not simply "before or after 4.0". CMake
   4.2.20260109 requires `d0edc3af-4c50-42ea-a356-e2862fe7a444`; sensen's
   conditional assigns that only to CMake < 4.0 and picks `451f2fe2-…`
   otherwise. Verified by configuring a minimal project against each candidate:
   any other value is rejected with "set to incorrect value". With the wrong
   value CMake **silently** declines the feature — no diagnostic names
   `import std`.

2. **libc++'s shipped module manifest is unusable as-is.**
   `/usr/lib64/libc++.modules.json` declares
   `"system-include-directories": ["../share/libc++/v1"]` — relative. CMake 4.2
   resolves `source-path` against the manifest directory but not that, and
   aborts with "Found relative path while evaluating include directories",
   naming neither libc++ nor `import std`. Workaround: rewrite the manifest with
   absolute paths and point `CMAKE_CXX_STDLIB_MODULES_JSON` at the copy.

3. **`-stdlib=libc++` must be in `CMAKE_CXX_FLAGS` before `project()`.** Via
   `add_compile_options()` it never reaches the synthesized std-module target,
   which is created during toolchain detection. The std module then gets built
   against **libstdc++** while everything links libc++, failing at link with
   undefined `std::__glibcxx_assert_fail` and `std::locale::~locale`.

4. **CMake's std module is `gnu++23`-only.** CMake 4.2 compiles its synthesized
   std module with `-std=gnu++23` and offers no working way to disable GNU
   extensions for it — `CMAKE_CXX_EXTENSIONS` before and after `project()`, and
   `CMAKE_CXX_EXTENSIONS_DEFAULT`, were all tried and the flag does not move.
   This project, sensen and SGEE each independently set `CXX_EXTENSIONS OFF`, so
   every consumer compiles `-std=c++23`, and a BMI cannot cross that boundary:

   ```
   error: GNU extensions was enabled in precompiled file '...bmi'
          but is currently disabled
   ```

   The cascade from this is severe and misleading: once `import std;` fails to
   load, every `std::` name reads as undeclared, and the failure propagates far
   enough that a plain `#ifdef` parses as a declaration
   (`unknown type name 'ifdef'`). It presented as 28 unrelated source errors
   across 27 files. It was one fault.

## The Actual Blocker

`sensen` has no `import std` path in the working-tree version this project
builds. Its modules rely on the header-std fallback, and the `std.pcm` machinery
its comments describe (`std_module_precompile`) is **not defined** in that
version. So dropping `SENSEN_NO_IMPORT_STD` makes every sensen translation unit
fail with `module 'std' not found`, and the fix would have to be inside sensen —
a submodule carrying an unfinished refactor that this work is not touching.

**Correction to an earlier claim in this investigation.** A review pass reported
that `std_module_precompile` *is* defined, at `sensen/CMakeLists.txt:1207`, gated
on `if(NOT CMAKE_CXX_MODULE_STD AND EXISTS "${LIBCXX_MODULE_DIR}/std.cppm")`.
That is true **of sensen's committed HEAD** and false of the working tree. Both
readings were correct about different versions; the version that matters for
this build is the working tree. Noted because the discrepancy is easy to
rediscover and waste time on.

Two further consequences worth recording:

- Because the parent forced `CMAKE_CXX_MODULE_STD ON`, it would have **disabled**
  sensen's own std.pcm flow even where that flow exists — that gate is
  `NOT CMAKE_CXX_MODULE_STD`. Forcing CMake's std module on is the one thing a
  parent must not do here.
- A shared, parent-built std.pcm is not obviously safer than per-subproject ones.
  BMI compatibility is stricter than dialect and standard library alone: both
  submodules record their own hard-won evidence that `-march`/SIMD feature flags
  (`sensen/CMakeLists.txt:969-971`) and `-DNDEBUG`
  (`external/SGEE/CMakeLists.txt:799-803`) also participate. Each subproject
  building its own std.pcm from its own flag set is compatible with its own
  consumers by construction.

## What Was Kept

Nothing in `backend/CMakeLists.txt`. The three genuine SGEE fixes from the
previous session stand on their own merits and are already committed there
(`86ee3120`), independent of this investigation:

- `add_dependencies(sgee sgee_std_module)` — `ninja sgee` never built `std.pcm`.
- Guarding `add_custom_target(std_module …)` — SGEE claimed a generic global
  target name that sensen already defines, aborting configure under CMP0002.
- Replacing a hardcoded `-fprebuilt-module-path` into one consumer's build tree
  with `SGEE_LOGGER_MODULE_DIR`, supplied by the parent.

## An Error Made And Recovered

While reverting a one-line experiment in `backend/sensen/CMakeLists.txt`,
`git checkout -- CMakeLists.txt` was used. That file was **already dirty** with
in-flight work, so the checkout reverted the whole file to HEAD and discarded
~92 lines of someone else's uncommitted changes. The damage was visible
immediately: the default configure began failing with
`set_target_properties can not be used on an ALIAS target` at
`sensen/CMakeLists.txt:1255` — a defect present in HEAD that the in-flight
version had removed.

Recovered from the NAS backup (`scripts/backup_to_nas.sh`, run earlier the same
session at 22:38, which mirrors submodule working files), restoring the file
byte-for-byte and returning the submodule to its prior dirty state. The default
configure passes again.

Lesson, worth more than the investigation: **`git checkout -- <file>` is not an
undo for one edit — it discards every uncommitted change in that file.** In a
tree with dirty submodules, revert a specific edit with a targeted edit, or stash
first. The NAS backup being current is the only reason this was recoverable.

## Verification At Close

- `cmake -S backend -B backend/build` configures cleanly.
- `calculator_engine` and `smoke_client` build with zero errors.
- Local smoke test: all four RPCs return live data
  (`SPY 731.60` from Alpaca; 3M CMT `3.83%` published / `3.7938%` continuous,
  `as_of 2026-07-29`).
- Deployed backend `api.optionsandfuturescalculator.com`: `HTTP/2 200` with real
  Alpaca and Treasury payloads.
- Deployed UI: both `3bvf084cqw36l.js` and `1css5855mxqb_.js` serving from the
  apex carrying `GetRiskFreeRate` and the `$/1% IV` unit label.
- Both submodules left in exactly the dirty state they started in.
