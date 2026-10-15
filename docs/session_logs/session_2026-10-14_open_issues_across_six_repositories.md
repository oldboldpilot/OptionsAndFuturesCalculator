# Open issues closed across six repositories, and the four that were hiding each other

*2026-10-14. Author: Olumuyiwa Oluwasanmi.*

The task was "fix all the open issues". This log records what was open, what the measurement said
rather than what the record said, and what is left for the owner.

Names from other repositories are spelled WITHOUT backticks. A backtick asserts the name exists in
THIS repository's tracked files, and the citation gate reads one repository only.

## What the issue trackers actually said

Neither tracker held anything. GitHub issues on ThinButQuickWebFramework stop at #7, all closed, and
the gitea API returned `count=0` open for all four of OptionsAndFuturesCalculator, sensen,
ThinButQuickWebFramework and mortgage-nest-egg. The numbered items this work refers to (#128, and the
#106/#109/#110/#111/#116/#121 of the previous session) are an internal register kept in prose, in the
repositories' own roadmaps, changelogs and session logs -- not tracker issues. Worth writing down,
because "check the tracker" is the obvious first move and it answers zero.

## sensen: the gateway store had two defects, and one of them was mine

**Defect #128, the store never reconnected.** `GatewayStore::open()` connected once and nothing ever
reopened, so after PostgreSQL restarted every call on the handle failed for the rest of its life. The
serving gateway escaped only because its policy poll opens a fresh store every ~15 s; the admin reaper
holds ONE handle for its whole life, so its ticks failed forever until the unit restarted.

The fix retries a failed statement once on a new connection, under three conditions that are each
load-bearing: the connection must be GONE (`conn.ok()` false -- the transport, never a server-side
refusal, so a unique violation, a CHECK, a 40001 serialisation failure and a 42501 are never
retried), there must be no open transaction, and a reconnect cannot start another. `commit` is
therefore never retried: a commit whose reply was lost may well have been applied.

What made this more than a retry loop is that every session property now comes from ONE place, called
by `open` and by the reconnect alike. Three of those properties are silent when absent -- an unset
statement timeout blocks forever, a session_replication_role of `replica` skips every foreign key
and every trigger, and an unset tenant scope makes row security fail closed so the store reads empty
-- so a reconnect that applied a subset would be worse than no reconnect at all.

**The new test found a defect in the fix.** Section 21's drift arm asserts the error CODE, not merely
that the call failed. It failed: a reconnect that reaches a store at a different schema version was
being flattened to `Unavailable` by the error builder. That message sends an operator to the network
when the DSN is what moved. An assertion written as "it refused" would have passed and shipped it.

Mutation arm, run: with the retry deleted, 21a fails with the transport's own reset message and 21b
reports no gateway backend at all. Source restored, md5 compared.

**A no-op that demanded a privilege.** PostgreSQL checks CREATE on the DATABASE before it evaluates
`if not exists`, so `create schema if not exists` failed with 42501 for a role without that privilege
even when the schema was already there -- and `admin migrate` over an up-to-date store is exactly what
an operator repeats. Both bookkeeping creates are now reached only when they would do something,
asked of pg_catalog, which every role can read. The repository's own §13 had recorded this as "still
open, as a choice", together with the reason it is not a complete fix: past these two statements the
pending migrations alter existing tables, and ALTER requires owning them. That half is kept.

Gates: test_gateway_store_pg **900 passed, 0 failed** (was 887), full build 0 errors,
test_registry_state 9/0 and test_registry_multinode 16/0 on an SGEE-enabled build.

**Two documents corrected.** GATEWAY_REGISTRY.md claimed "8/8 on the real code" under a table of nine
cases beside a list of seven mutants -- a figure matching none of them; and it never mentioned
test_registry_multinode at all, so a reader came away believing the registry's only replicated gate
was the three-node one. That file is the G0 multi-node gate and it exists because three nodes over
real sockets cannot see three classes of defect: at three nodes `n/2 + 1`, a hardcoded 2 and
`(n+1)/2` are all 2, one shared clock makes a locally-derived decision identical on every replica in
the harness and different on every replica in production, and a cluster is a routing dimension where
SGEE keys transports by node id alone.

## fastjson: four build defects, each hiding the next

The open item read "fastjson has no runner for the documentation gate". The reason it had none is
that `ninja` never reached a test binary, and the reason for THAT took four fixes:

1. `ENABLE_CUDA` defaulted ON, so `enable_language(CUDA)` ran on every host and a box without nvcc
   died at configure time. It auto-detects now, looking for a COMPILER and not a device. An explicit
   `-DENABLE_CUDA=ON` still fails loudly -- silently dropping to OFF would turn a configuration
   error into a missing feature.
2. The vendored copy of libc++'s own std.cppm was a stale snapshot. That file guards each
   not-yet-available header with an `#error` addressed to the libc++ maintainer, so a snapshot cannot
   survive libc++ gaining a header: on libc++ 23 `<text_encoding>` exists and the snapshot stopped
   compiling, which then read as "module 'std' not found" on every module in the project. The
   INSTALLED interface is compiled now, being versioned with the headers it describes.
3. std_module was ordered but not LINKED. The file carried a note calling the resulting cold-build
   failure a known CMake/Ninja limitation to be worked around by running ninja twice. It is a missing
   link edge: `add_dependencies` orders targets while the `import std` scan is scheduled
   independently. Its two siblings had always linked it and never showed the failure -- the evidence
   was in the same file as the note.
4. The library and the benchmarks compiled at DIFFERENT ISA feature sets (the library at a
   per-feature accumulation with no `-mfma`, everything else at -march=x86-64-v3 ... -mfma). A BMI
   records its target features, so that is two incompatible worlds in one tree and ninja ended with
   20 errors. The file already stated the rule 500 lines up, about a different target.

Then benchmarks/turbo_benchmark.cpp did not compile at all: it names __m256i with nothing declaring
it, and its ten string-mask sites needed the `pclmul` feature that x86-64-v3 does not carry. Raising
the baseline was not an option -- sensen has already recorded a submodule appending `-maes -mpclmul`
and splitting a shared std.pcm from the units importing it -- so the intrinsic is used only under
__PCLMUL__ with the portable prefix-XOR otherwise, VERIFIED equal against the intrinsic compiled
with `-mpclmul` over all 65,536 low-16 patterns, every single-bit input, 2,000,000 random words and
six boundary values at both widths: 2,065,574 comparisons at 32 bits and 2,065,606 at 64, zero
mismatches.

`-ffast-math` was removed from both flag sets. The policy forbids it, and the reason bites here more
than most: it lets the compiler assume no NaN and no signed zero and reassociate freely, and this
library parses and prints the very doubles those assumptions are about.

Clean tree, after: cmake 0 errors, ninja 141/141 with 0 errors and 0 warnings in the repository's own
sources, ctest 2/2. The README's Makefile example, which told consumers to compile at
`-march=native` against a library whose BMIs are built at the baseline, names the baseline now.

## cpp23-logger and fastjson: the documentation gate has a runner

Both carried `tools/check_docs_claims.py` and its self-test, and both named them in their own
config/cpp_details.txt and config/update_policy.txt, and in neither could the suite run them -- so
they passed whenever someone remembered, which is indistinguishable from a gate that passes. Both now
register the self-test as the ctest `check_docs_claims`, guarded by PROJECT_IS_TOP_LEVEL so a
superproject runs its own copy against its own documentation and not theirs.

fastjson had registered NO tests with ctest at all; test_turbo_correctness was built and never run.

Positive control on the registration, not just on the script: with the self-test replaced by a stub
that exits 1, ctest reports 5 - check_docs_claims (Failed) docs policy. Restored, md5 compared.

## ThinButQuickWebFramework: a kill-by-name, nine warnings, and a stale copy of the rules

`ninja kill` was pkill -9 unified_server -- a kill by process NAME on a box where every checkout and
every lane worktree builds a binary with that name. The same class has already cost this box a gate.
The pid now comes from `/proc/<pid>/exe` compared against this build's own binary, with SIGTERM before
SIGKILL and `exe` re-read between the two because a pid can be recycled. Verified with a NEGATIVE
control, which is the one that matters: two processes named unified_server at different paths, and the
one at the other path was untouched.

The repository's warning debt turned out to be **9, not the 403 previously recorded**. Of the ~520,000
warnings a full build emits, 9 are in sources it owns and the rest are vendored (505,770
`-Wextra-semi` alone); there were ZERO `-Wmissing-designated-field-initializers`, which an earlier
note had put at 230. All nine are `-Wunused-parameter` in five stubs that return "not yet
implemented", and all five are REACHED, so they are honest refusals rather than dead code: the
parameters are `[[maybe_unused]]` and still named, because the signature documents the contract an
implementation will have to meet.

A ten-month-old copy of the agent instructions sat at docs/development/CLAUDE.md, where a coding agent
working under that directory would load 19,002 bytes of superseded rules beside the live ones -- and
its own sync policy named a project-root CLAUDE.md that no longer exists while calling itself
ai/CLAUDE.md, which it is not. It is now docs/deprecated/AI_ASSISTANT_GUIDE_2025-12-09.md with a
banner, and the eight names the citation gate reported as gone lost their backticks.

## What was NOT caused by this work, and is left where it is

The sensen policy subset reports 8 failures out of 110. None name a file this work touched, and two
are informative:

- policy_odr_no_duplicate_symbols: the `sen` binary and libsensen_text_search.so.1 both define the
  initializer for module sensen.logic. That library comes from a concurrent session's lane/ts-fast-scan
  work, so this is their in-flight defect; fixing it here would collide with them.
- policy_slim_groups: sensen.cpu_topology is reachable from the roots and in no group. Same
  neighbourhood, same reason to leave it.
- policy_check_added_lines_selftest fails because the gate emits `[FAIL]` for rule 3/4 where its own
  self-test expects `[WARN]` -- a gate and its self-test that have drifted apart. Real, pre-existing.
- policy_no_c_alloc_facts and policy_no_c_stdio_facts fail on a fixture that expects an error
  message naming a missing CUDA compile database; this host has no nvcc. Their _tree variants skip
  for the same reason.
- policy_lane_completion reports on OTHER lanes' completion records, judged at their own last change.
- policy_python_installed_import needs a python install this build dir does not produce.

## For the owner

- **mortgage-nest-egg tracks .env, .env.development and .env.production in git**, not gitignored, and
  this tree's own notes record that its "sandbox" Stripe variables hold LIVE values. The repository is
  PRIVATE on GitHub, which lowers the urgency and does not change the shape. Not touched, deliberately:
  the Railway build may read them, so `git rm --cached` could break a production deploy, and removing
  them from history is a rewrite. An owner decision with a production consequence.
- The four items the previous session left (the OFC backend/sensen pin 1,501 commits behind, the
  sensen archive branch, tbqwf's unmerged wip/tbmigpg-prototype, and stash@{0}) are unchanged. The pin
  bump in particular implies a Railway production deploy of the options calculator.
