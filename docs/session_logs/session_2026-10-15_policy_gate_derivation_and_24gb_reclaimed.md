# 2026-10-15 — a lane's diff re-derived from another lane's merge, and 24 GB of review trees retired

> Paths and symbols inside `backend/sensen` are written without backticks throughout: the submodule is a single
> gitlink entry in this repository's index, so `tools/check_docs_claims.py` cannot resolve a name inside it and a
> backtick would be a claim this repository cannot support.

## What was wrong

policy_lane_completion in sensen reported **886 problems across 8 completion records**. 846 of them were false, and
they all came from one function.

own_diff_base in backend/sensen/tools/check_lane_completion.py reconstructs a landed record's own diff. For a record whose commit
is already on origin/master it looks for "the first merge ahead of that commit on master's first-parent chain" and
treats it as the lane's landing merge. Lanes in this repository land by **fast-forward**, so master's first-parent chain
descends *into* lane branches, and the merge found is normally a a "Merge origin/master into lane/other" commit — whose **first**
parent is a different lane. Two consequences, from one cause:

- **Over-reach.** the merge-base of that merge's first parent and the head lands before this lane existed. One record was charged with 845
  "exports X but it is not listed in api" problems belonging to every lane that had landed in between; a documentation
  record was accused of changing backend/sensen/agent/CMakeLists.txt and three python tests it never touched.
- **Vacuous pass.** For the other 17 of the 20 records judged through this path, the derived base already contained the
  head, so the range was **empty** and every diff check passed having examined nothing — the exact failure the function's
  own docstring says that branch exists to prevent.

Measured across all 80 records: **not one** of the 20 merge-derived ranges was usable. 17 empty, 3 over-reaching by
224, 18 and 4 commits (containing 34, 2 and 1 other lanes' merges).

## The fix

The derivation is **validated instead of trusted**. A range that already contains the head, or that begins before the
base the record itself declares, cannot be that lane's diff, so own_diff_base raises LandedWithoutMerge — the honest
path its docstring already specified — and the caller prints a NOTE. docs_only_diff is given the declared base for the
same reason. 886 problems → 40, every one of the 40 being record content in records owned by other lanes.

**Why the gate's own self-test never caught it:** the case named "a head landed by a merge" built that merge with the
repository's **root commit** as first parent. No landing merge has that shape. It passed only because the over-reach
happened to contain the lane as well. The fixture now builds the merge the way a real one is built, and two new cases
cover the guard — the unrelated-merge shape and the empty-range shape. Disabling the guard fails both, the first with 16
false export problems and a false non-documentation refusal: the real symptom in miniature.

Confirmed live afterwards: master moved mid-lane and brought in a brand-new record (cc-cache-worktree-bmi.json) whose
own landing is a a "Merge origin/master into lane/..." commit. It would have read an empty diff; it now carries the NOTE.

## Three more gates, red for their own reasons

- **sensen.cpu_topology was in no slim group.** backend/sensen/cmake/SensenSlim.cmake. Rule 64's one topology reader is imported by
  backend/sensen/src/parallel.cpp and by sensen.gemm, so backend/sensen/tools/sensen_slim_closure.py reported it reachable from the roots and not
  built — a slim build links with its reader undefined. It imports only std, so it belongs in FOUNDATION with its
  implementation unit. The self-test's own "unmodified registry is clean" baseline case was failing on it.
- **The raw-allocation check failed runs it is documented to warn on.** backend/sensen/tools/policy_check.sh said FAIL on added
  new/delete/malloc, while the same file records that check as a WARN **twice** — the Rule 2 block contrasts itself
  against it ("FAIL, not WARN: unlike the raw new/delete check above, a grep CAN decide this truthfully") and the rule
  61/H1 block lists it among the judgement-adjacent checks — its three siblings all WARN, and its own verdict text asks
  a human to confirm each site. Any lane that added an allocator site failed the whole run.
- **A failure counter that was a boolean.** backend/sensen/tools/check_policy_added_lines_selftest.sh used fail=1, so three
  failures were reported as "failed: 1", and that count was its exit status, which exits 0 at 256.

## Corrections to the earlier record in this session

- The two code_facts policy gates were reported as failing "because the fixture expects a missing-CUDA-compile-DB
  error and there is no nvcc". **Wrong.** Their fixture writes its own synthetic compile database and needs no nvcc; they
  **skip** (77) on this host because libsensen_code_facts.so is built nowhere on it, and their ctest registration
  carries SKIP_RETURN_CODE 77. Nothing here fixes or needs to fix them.
- policy_lane_completion was attributed to "other lanes' records". That was true of 40 problems and false of 846.

## 24 GB of review trees retired, after checking rather than assuming

`/home/muyiwa/.cache/review-gwcfg2` (24 GB, three nested clones, ~650 MB of build logs) had been left alone twice for
fear it held uncommitted work. It did hold 59 modified or untracked files in `src`. Every one was checked against sensen
master:

- all 15 untracked files (new modules backend/sensen/agent/src/gateway_duration.cppm, backend/sensen/agent/src/gateway_reload.cppm, their tests, two session logs)
  are in master and **byte-identical**;
- both clean clones' HEADs (f4147101b, ffc218ea2) are ancestors of master;
- of 2,585 added lines across the 44 modified files, **58 are absent from master, and all 58 are superseded drafts** —
  they say "migration 15" where master says 16 (round 5 revised them), plus one CLAUDE.md line whose content master
  carries in backend/sensen/docs/ENGINEERING_NOTES.md after that file was slimmed.

The working diff was saved to /home/muyiwa/.cache/review-gwcfg2-src-uncommitted.patch (572 KB) and the directory
deleted. `/home/muyiwa/.cache/review-gwtime-tools` (92 KB, 16 one-off analysis scripts) is **kept**: none of those files
exists in any repository, so it is their only copy.

## Gates

Three repeats of each, identical: slim closure `OK`, slim self-test `PASS`, added-lines self-test **21 passed / 0
failed** (was 18 passed / 3 failed, reported as 1), lane-completion self-test `PASS`. backend/sensen/tools/policy_check.sh on the
lane's own diff rc 0; check_commit_identity 2 commits clean. Citation gate `checked=32 missing=0 design=0 fragment=0
revision=0`, proven non-vacuous by a planted name that reported MISSING 1.

**Still red, and not this lane's to fix:** policy_lane_completion reports 40 problems in 6 records owned by other
lanes (fastjson-bump 16, sen-code-providers-auto 12, acr-tools-round2 6, acr-m11-triton 3, text-search-core 2,
acr-m6a1-ladder 1) — paraphrased call_site quotes and symbols the record's named file does not mention. A completion
record is a claim by its author; editing someone else's evidence to make a gate green is the failure these gates exist
to catch. policy_odr_no_duplicate_symbols and policy_python_installed_import need built binaries and were not run.

sensen landed at 9cf23f403 on origin and gitea (verified by ls-remote), NAS-synced rc 0 with the five changed files
compared by md5.
