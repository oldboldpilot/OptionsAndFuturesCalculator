# Making the vendored llama.cpp compile under clang 23 / libc++ 23

@author Olumuyiwa Oluwasanmi

Written 2026-08-29, while moving the toolchain from clang 22 to clang 23.

This is the method, not just the outcome. The same shape applies to the next
libc++ release and to any other pinned third-party C++ this repo vendors.

---

## 1. The symptom, and why it is not llama.cpp's fault

Building the tree with clang 23 produced **221 errors**. Every one was inside
`_deps/llamacpp-src`. Zero were in this repo, sensen, SGEE, gRPC, protobuf or
abseil.

Deduplicated, 221 collapses to **thirteen sites in seven files** and two
messages:

```
error: use of undeclared identifier 'getenv'      (also 'atoi', 'strtol')
error: no member named 'max' in namespace 'std'
```

| file | needed |
| --- | --- |
| `src/llama-context.cpp` | `<cstdlib>` |
| `src/llama-graph.cpp` | `<cstdlib>` |
| `src/llama-graph.h` | `<cstdlib>` |
| `src/llama-hparams.cpp` | `<algorithm>` |
| `src/llama-kv-cache.cpp` | `<cstdlib>` |
| `src/llama-model-loader.cpp` | `<cstdlib>` |
| `src/llama-vocab.cpp` | `<cstdlib>` |

**libc++ removes transitive includes every release.** A header that used to drag
in `<cstdlib>` no longer does, so code calling `getenv` without including it
compiles on libstdc++ and on older libc++ and stops compiling on the new one.
The bug was always there; the older standard library was hiding it. Nothing here
is llama.cpp-specific and nothing upstream would dispute it.

**Read the error COUNT before reading the errors.** 221 sounds like an
incompatible dependency. Thirteen sites in seven files, all missing includes, is
a morning's work. The first number is an artefact of one header
(`llama-graph.h`) being included by 190 translation units — 190 of the 221 came
from that single file.

---

## 2. The decision that mattered: patch, do not bump

The obvious fix is `GIT_TAG` → a newer llama.cpp. It was rejected, and the
reason is the whole point of this document.

**llama.cpp is in this tree to be an INDEPENDENT implementation.** CLAUDE.md is
explicit: its "one legitimate role is being an INDEPENDENT implementation for
the parity probes — a reference that shared sensen's code would prove nothing."
It is not a serving alternative and not a conversion step. Its value is that
when sensen and llama.cpp agree on a token, the agreement means something.

Moving the pin is therefore **changing the reference**. If a parity probe
disagreed afterwards, nothing would say whether sensen changed, llama.cpp
changed, or the comparison was never valid. Seven `#include` lines cannot cause
that: they add no code path, change no arithmetic, and emit no different
instruction.

The general rule: **when a dependency exists to be a control, prefer the change
that provably alters nothing over the change that merely probably does.**

---

## 3. The mechanism

`backend/patches/llamacpp-b6963-libcxx23-includes.patch`, applied by
`FetchContent_Declare`'s `PATCH_COMMAND` in `backend/CMakeLists.txt`:

```cmake
set(LLAMACPP_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/llamacpp-b6963-libcxx23-includes.patch")
FetchContent_Declare(
    llamacpp
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG 6db3d1ffe6d34e8becfe95cf2f109198a7154db3 # b6963
    PATCH_COMMAND git apply --whitespace=nowarn "${LLAMACPP_PATCH}"
               || git apply --reverse --check "${LLAMACPP_PATCH}"
)
```

Three properties, each deliberate:

**The patch is part of the pin.** The commit hash alone no longer describes what
gets compiled, so the patch lives beside it in the repo rather than in somebody's
working tree. A fresh clone builds.

**It is idempotent by construction.** `git apply` succeeds on a clean checkout.
When the source directory is already populated and patched — which is what a
re-configure sees — `git apply` fails and `git apply --reverse --check`
succeeds, so the command as a whole succeeds. A genuinely conflicting tree fails
both and stops the configure, which is the direction to fail in.

**It was proven against a pristine clone, not against the tree that already had
it.** Patching a working copy and then declaring the patch good tests nothing:
of course it applies, it came from there. The check is:

```bash
git clone --no-checkout https://github.com/ggml-org/llama.cpp.git src
cd src && git checkout 6db3d1ffe6d34e8becfe95cf2f109198a7154db3
git apply --whitespace=nowarn <patch>                    # -> OK
git apply <patch> || git apply --reverse --check <patch> # -> OK (already applied)
```

---

## 4. The standard bump, and the cost that was counted rather than argued

`LLAMACPP_CXX_STANDARD` (default **23**, up from 17) controls the standard these
sources compile at. A cache variable, so the choice is one flag rather than an
edit somebody has to find.

The previous C++17 pin had a real, documented reason: `CMAKE_CXX_SCAN_FOR_MODULES`
is ON globally, and a C++20-or-later effective standard is what makes CMake's
Ninja generator scan a source for `import` / `export module`. Counted in
`build.ninja`:

| | module-scan steps |
| --- | --- |
| llama/ggml at C++17 | **1** |
| llama/ggml at C++23 | **486** |
| whole tree | 3,639 → **4,124 (+13%)** |

...for ~200 translation units containing no modules at all.

Accepted knowingly. It buys one standard across everything this repo compiles,
which is what `config/cpp_details.txt` rule 50 asks for; the cost lands on
configure/scan rather than on the deploy image, where
`ENABLE_LLAMACPP_BACKEND=OFF` means none of it is compiled at all; and
`-DLLAMACPP_CXX_STANDARD=17` reverts it.

**The original C++17 comment is retained verbatim in `backend/CMakeLists.txt`
above the new one.** The mechanism it describes is still true, and a reader who
sees only the new decision cannot tell whether the old one was wrong or merely
outweighed.

---

## 5. What was deliberately NOT done

llama.cpp's sources are **not** rewritten to `config/cpp_details.txt`'s house
rules — trailing return types, `[[nodiscard]]`, `std::expected` instead of error
codes, `import std`, no raw pointers, no external test framework.

Those rules govern code this project owns. Applying them here would mean
restyling roughly 200,000 lines of upstream C++, redoing it on every bump, and —
the decisive objection — **eroding the independence that is the only reason the
dependency is vendored.** A reference implementation rewritten in our idioms is
no longer independent of us.

Compiling it at our standard is the part of "C++23 compliance" that is both
meaningful and free of that cost. The rest would buy style at the price of the
property the dependency exists for.

---

## 6. Gates

Four arms, all **103/103**, because the local toolchain and the image toolchain
differed during the transition and both had to hold:

| arm | ctest |
| --- | --- |
| clang 23, `ENABLE_LLAMACPP_BACKEND=OFF` | 103/103 |
| clang 23, ON at C++17 | 103/103 |
| clang 23, ON at C++23 | 103/103 |
| clang 22, patched, ON at C++23 | 103/103 |

The C++17 arm is not redundant: it separates "the includes fixed the build" from
"the standard bump fixed the build", and only the first is true.

---

## 7. Doing this again

When the next libc++ release breaks a vendored dependency:

1. **Deduplicate the errors before reacting.**
   `grep -oE '^/[^:]+:[0-9]+:[0-9]+: error:' build.log | sort -u`. A four-figure
   count is usually one header times its includers.
2. **Check whether every error is in the dependency.** If your own code is
   clean, this is a third-party include problem, not a toolchain problem — and
   the toolchain is not what needs reverting.
3. **Prefer a patch to a bump for anything that serves as a control.** State
   what the change *cannot* affect, rather than hoping it does not.
4. **Prove the patch on a pristine checkout**, and make the command idempotent
   so re-configure is not an error.
5. **Count the cost of a standard bump** in `build.ninja` steps before taking
   it, and leave a flag that reverts it.
