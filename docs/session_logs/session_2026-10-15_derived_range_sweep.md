# Session 2026-10-15 — the derived-range sweep finished, and a twin check that had never run

@author Olumuyiwa Oluwasanmi

The queued self-improvement task from earlier the same day was worked to completion in sensen. Nothing in this
repository changed except this log: the sensen submodule pointer here still sits at its own pin, which is deliberate
(moving it implies a C++23 engine rebuild and a production deploy).

Paths under the sensen submodule are written WITHOUT backticks throughout. This repository's documentation citation
gate resolves a backticked name against this repository's own file list, which sees backend/sensen as a single gitlink
entry, so a backticked submodule path would be reported MISSING.

## What the task asked, and what carried the defect

Eleven sites were named as deriving a range, a base or an "own work" set from git history. Four carried the defect:

- the three lane commands (sen lane merge, sen self verify in both of its places, sen review) each derived a lane's own
  base as the merge base of the target branch and the lane tip, and checked only that a base came back. When the lane
  tip is already contained in the target — a merge re-run, a fast-forward landing, work another lane carried across —
  that merge base IS the tip, so the range is empty and every judge reading it examines nothing. The acceptance consult
  then denied for no_author_observed, which is true and names a symptom rather than the cause.
- the worker's commit-present check read `<launch base>..HEAD` as the session's own work with no test that the base is
  still an ancestor of HEAD. Measured on the parent against a base on a sibling branch: it reported "1 own commit(s)"
  and VERIFIED, because the exclusion list cannot see an over-reach whose extra commits sit on no other ref.

The other seven already validate or raise. That is a result too, so each is recorded in the completion record with what
makes it safe — otherwise the next sweep re-reads all eleven.

## The fix

One derivation, laneOwnRange, in the worktree module: it resolves the tip first (so a caller passing a sha and one
passing a branch cannot disagree) and returns a refusal naming the cause when no honest range exists. Returning a
refusal rather than an empty range is the whole point: a degenerate base must not be readable as a small one.

Gates: the worktree test 340 passed (314 on the parent's test file), the worker test 775 passed (773 passed / 2 failed
on the parent source), the lesson test 85 passed (70 before). Three repeats of each binary and three ctest runs of the
five registered gates. Five mutation arms, two of them in opposite directions on the new derivation — removing the
degenerate-base refusal turns one check red, making the base follow the target's tip turns three others red, and the
first check stays green under the second arm, which is what shows the arms are independent.

## The lesson files were in no gate that runs the parser

The lesson test named one file literally, so the lesson file shipped earlier the same day was never parsed by a gate:
with one clause terminator dropped it reported 70 passed, 0 failed and exited 0. It now enumerates the lessons
directory. An enumeration is itself the vacuous-pass shape, so it asserts the directory is readable, the count is
positive and one known file is among the names — with the directory emptied the section fails. Per file a control drops
a clause terminator from the shipped bytes and requires a refusal naming a position, and the terminator is taken from
the first line that is NOT a comment, because a comment line reaches no parser and a control that edited one would
mutate nothing.

## The twin check had never run on this host

Closing the record surfaced that the rule-64 twin check (C9 in the pre-landing gate) had never run here: it needs a
compiler-facts directory. Both pieces build on this box, and a plugin build of the agent library over the lane tree
produced 1,494 fact files, so C9 ran for real and reported no twin — the right answer for a lane that replaced three
derivations with one, and worth nothing without a control.

The first control attempt planted a struct reusing an exported name and reported NOTHING. The plant was uncommitted,
and the tool takes its change set from the committed diff. Re-done as a commit built with git commit-tree off HEAD, so
the working tree was never touched, the control is reported with exit 1 naming the other definition's file, line and
qualified name. A gate's silence is only evidence once something has been shown to break it, and a control applied
where the gate does not look is not a control — which is the same mistake, in miniature, as the one the whole lane is
about.

## State

- sensen master: 36428271302dc303fc99416c4e129e808c489e4a on origin and gitea, both confirmed by ls-remote. 13 changed
  files rsynced to the NAS, all 13 md5-matched.
- The lesson is active in the working store (imported trusted and activated) and answers from the store: three field
  signals applicable, a control signal not.
- The task row stays `queued`: there is no "sen task done", and the states advance through the lane machinery. Its spec
  now carries a landed note naming the commits, so the queue does not read as untouched work.
- Whole-tree completion check: 91 records, 9 incomplete, 44 problems — none of them in this lane's record. Those nine
  belong to other lanes and are their authors' claims to correct.
- Still open, and named in the record: driving the three callers' refusals through the commands themselves, which needs
  a store, a lane row and a task lease.
