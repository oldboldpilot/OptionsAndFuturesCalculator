# Session log — 2026-10-15: why 846 of 886 gate problems were false, and the recurrence guard for the class

Author: Olumuyiwa Oluwasanmi

Note on citation style: this log cites paths inside the sensen submodule without backticks, because the documentation
gate resolves a backticked name against this repository's own file list, which sees backend/sensen as a single gitlink
entry. This log was checked with `tools/check_docs_claims.py`, as `CLAUDE.md` requires, and the sensen-side documents
were checked with sensen's own copy of it.

## The question

Why were 846 of the 886 problems the sensen gate policy_lane_completion reported false, and can the class be queued as
self-improvement work for the agent so it does not recur?

## Why they were false, measured on master

The gate rebuilds a landed record's own diff from the first merge ahead of that record's commit on master's
first-parent chain. Lanes land in sensen by fast-forward, so that chain descends INTO lane branches, and the merge it
finds is normally another lane merging master into itself — whose FIRST parent is that other lane.

Worked through for the worst record, fix-flatpool-resize.json, judged at its own last change d6dbf3b0c:

| quantity | value |
|---|---|
| merge the old code picked | "Merge origin/master into lane/agent-openrouter-zdr" |
| base the record declares | 378f4b826f — 67 commits of the lane's own work |
| base the derivation implied | 669f0356c9 — **157 commits earlier** |
| range it produced | 224 commits containing **34 other lanes' merges** |
| problems that produced | 845 of the form "exports X (added or changed by the diff) but it is not listed in api" |

The other symptom is the same bug: for 17 of the 20 merge-derived records the implied base already contained the head,
so the range was EMPTY and every diff check passed having examined nothing. Over-reach and vacuous pass are one
defect. Not one of the 20 derived ranges was usable.

## What landed for the class (sensen master 5a965dbca, both remotes verified)

1. A lesson in the rule base the agent consults — agent/policies/lessons/lessons_2026_10_15.pl, lesson
   derived_history_fact_unvalidated: the symptoms that gate printed as signals, one cause, five anti-fixes (listing
   another lane's symbols in this record's api first among them), three fixes, two gates. Verified with the shipped
   engine: each field symptom answers "applicable", an unrelated control signal answers nothing, and on the parent's
   lesson base the same query exits 13 with "no lesson ... is loaded". It is inert until the owner imports and
   activates it.
2. A self-improvement task — docs/agentic/self_improve/si_derived_history_fact.json, kind sen-self-improve,
   priority 9, signature owner-2026-10-15:derived-history-fact-validation. It sweeps the nine other sites that derive
   a range, a base or an "own work" set from history, each with a gate built from the real landing shape plus the
   unrelated-merge and empty-range shapes. Filed in the checkout's queue as tsk_01m50c1tkabg7ev1rptmxxqmzt.
3. A gate that was missing entirely — tools/check_lesson_files.py, registered as policy_lesson_files and
   policy_lesson_files_selftest. A shipped lesson file was in no gate, because test_agent_lesson names one lesson file
   literally. What it requires is measured from the 28 lessons already shipped (title, signal and fix are on all 28;
   cause, gate, requires, anti_fix and evidence are on 8 to 26), so it does not open red over another author's file.

## Still open

- The sweep itself needs a C++ build and is queued, not done.
- test_agent_lesson still names one lesson file literally, so the real parser gates nothing new.
- policy_lane_completion reports 43 problems in 7 records owned by other lanes; the new record adds none.
