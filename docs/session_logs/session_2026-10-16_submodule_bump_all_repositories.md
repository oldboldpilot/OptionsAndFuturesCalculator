# Session 2026-10-16 — every submodule to its latest, and the two defects that surfaced

@author Olumuyiwa Oluwasanmi

Paths under the sensen submodule are written WITHOUT backticks throughout. This repository's
documentation citation gate resolves a backticked name against this repository's own file list, which
sees backend/sensen as a single gitlink entry, so a backticked submodule path would be reported
MISSING.

## What moved

Eight pin sites across four repositories, six distinct libraries. Here: backend/sensen 2058 commits
forward, backend/cpp23-logger 6, backend/nanobind 4, backend/external/SGEE 2. The same libraries
moved in ThinButQuickWebFramework, in sensen itself, and in nest-egg, and they were aligned
deliberately: two copies of one library at different commits in a single build tree is the defect the
first of the two findings below is about.

external/tbqwf stays unpopulated here. That is not an oversight: populating it adds -maes -mpclmul to
this checkout's flags and therefore to the shared std module, which is a different floor from the one
the rest of this build uses.

## A redundant compiler flag that broke a different repository

fastestjsoninthewest unified its module flags with its benchmarks' and added -mfma. The baseline it
sits beside, x86-64-v3, already enables FMA, so the flag changed no instruction it could emit. It did
change something else: clang records the EXPLICIT target-feature list in a module's binary interface,
and that check is one-directional. An interface naming a feature is refused by any translation unit
that does not name it too, while one naming fewer features loads in a consumer naming more. And
-march= does not count as naming it — it enables FMA and emits no feature of its own.

So ThinButQuickWebFramework, on the identical baseline, stopped building, with two feature errors
followed by every name from that library reported undeclared. The fix was at the source: drop the
redundant flags. Nothing was lost, because the baseline implies them.

The lesson generalises past this flag. A redundant per-feature flag on a module target is never free:
it buys no instruction and exports a requirement, in that exact spelling, to everyone downstream. The
new gate asks the COMPILER which features a target's own baseline already provides, because that is
the only way to separate this defect from a deliberate floor raise — the AES-NI the TBQWF crypto
kernels genuinely need is the same shape and must not be reported as a mistake.

## A path that reached into this repository from inside the submodule

The sensen bump then failed to configure here, at a target whose sources were named through
CMAKE_SOURCE_DIR. That variable is the TOP-LEVEL project's directory, not the submodule's, and this
repository builds sensen with add_subdirectory — so eleven paths that are correct when sensen is built
alone pointed into this repository's own backend/src instead.

The refusal was the lucky outcome. This repository's backend/src exists and simply has no file of the
name sensen wanted. Had it owned one, CMake would have compiled THIS repository's file as that
module, silently. Fixed in sensen against its own directory, verified both standalone and embedded.
Thirty-six further uses of the idiom remain there and are recorded in the file that was fixed; they
are invisible only because this repository configures a slim subset of sensen's targets, and widening
that subset is what exposed this one.

## What was measured, not assumed

sensen's suite reported 43 failing tests after the bump, which is the sort of number that invites a
guess. A baseline was built instead: the same commit, the same configuration, the ORIGINAL pins. 42
of the 43 fail there identically, so they are pre-existing or environmental on this host — 9 need
numpy, 3 need nvcc, 2 need Vulkan tooling, and the rest want a browser, a Landlock sandbox, a Windows
toolchain or a Python package install. The one difference was the new gate itself, which had not yet
been given its waiver. No regression from any bump.

Two attribution attempts before that were wrong and are worth recording. The first read the parallel
run's output, where ctest interleaves tests, so each failure was paired with another test's reason.
The second assumed output precedes a result line when in serial mode it follows, which paired every
test with its predecessor. The third run carried a control — one failure whose cause was known by eye
— and only then were the numbers used.

Here: 212 of 212 backend tests pass, the engine and its tests build with no failure, and the sensen
module closure check reports the slim groups still match the source tree.

## State

- This repository: pins bumped, pushed to origin and gitea, confirmed by ls-remote, changed files
  synced to the NAS and md5-matched. The frontend is untouched by submodules and was not rebuilt.
- sensen, ThinButQuickWebFramework and fastestjsoninthewest: pushed and synced, each verified by its
  own build and test suite.
- nest-egg: pins bumped and pushed. Its suite is identical to the baseline taken beforehand, 1991
  tests, and its production build and client-bundle check are clean.
- NOT done: the engine was not deployed. Moving the sensen pin here implies a deploy, and that is a
  separate decision with its own confirmation.
