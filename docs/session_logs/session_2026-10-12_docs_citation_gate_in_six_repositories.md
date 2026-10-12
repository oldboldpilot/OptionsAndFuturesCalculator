# Session 2026-10-12: the documentation citation gate in six repositories

## What was asked

Make the shared documentation-citation gate, `tools/check_docs_claims.py`, part of the recorded code policy of six repositories and carry a copy in each. The gate checks that the names a document cites exist in the repository. The script had been written for sensen only, so it first had to be made repository-agnostic.

The gate's lookup is textual. A pass proves that a cited name exists somewhere in the tracked files outside `docs/` and outside `*.md`. It never proves that the sentence around the citation is true. The semantic fact-check is still a human's.

## The policy placement

Each repository's `config/cpp_details.txt` gains one rule carrying the gate. The number differs by repository:

- rule 67 in sensen
- rule 61 in OptionsAndFuturesCalculator, ThinButQuickWebFramework and mortgage-nest-egg
- rule 1 in a newly created `config/cpp_details.txt` in fastestjsoninthewest and cpp23-logger, which had no config directory

The policy bodies of OptionsAndFuturesCalculator, ThinButQuickWebFramework and mortgage-nest-egg are byte-identical from the DESIGN PRINCIPLES heading on, so the rule text is identical in all three and the copies stay diffable.

Each repository's `config/update_policy.txt` gains the same bullet block under its "Documentation Updates" checklist item. That is item 3 in OptionsAndFuturesCalculator, ThinButQuickWebFramework and mortgage-nest-egg, and item 4 in sensen. fastestjsoninthewest and cpp23-logger get a newly created file.

The rule: a commit that changes a `*.md` file runs the gate and leaves MISSING at zero, or lists every remaining line with its justification in the commit message. Another repository's code names, and kernel and external-tool names, are the justified class.

## The five defects in the script

Each defect was measured before it was changed. Each has a mutation arm that was run, shown to fail, and then reverted byte-identically.

**(a) The default base.** The default base was a hardcoded merge-base with `origin/master`. ThinButQuickWebFramework and mortgage-nest-egg are on `main`, so the default invocation did not run there at all. It ended in an unhandled Python traceback. The base is now derived from the repository's own default branch: origin/HEAD, else origin/master, else origin/main. A repository with none of them is refused by name. Mutation arm: restoring the hardcoded ref fails 7 checks, one of them "the refusal is not a Python traceback".

**(b) The path prefixes.** The set of prefixes a citation could be checked against was a hardcoded list of twelve directory names taken from sensen's layout. In OptionsAndFuturesCalculator, a probe cited four paths under two of that repository's own top-level directories. Two were real and two were invented. None of the four was checked and none was reported, so an invented path read as a pass. The prefixes are now derived from the repository's own tracked top-level directories. A filename-extension test sits beside them, so a path in a directory this repository does not have is still checked. Mutation arm: restoring the hardcoded list fails exactly 2 checks, while "a real path under that same directory is found" still passes. That second check is the positive control. It shows why the defect hid: the hardcoded list let an invented path pass without making a real path look broken.

**(c) Git revisions.** A git revision was classified as an identifier, so every commit a document cites was a fatal MISSING. In one OptionsAndFuturesCalculator session log, 8 of the 22 missing citations were shas. A fourth bucket, REVISION, is now reported and is never fatal. It matches a span of 7 to 40 hex characters with at least one digit among them. A revision this repository resolves is not reported at all, so a mistyped local sha still shows up as MISSING or REVISION rather than passing quietly. A revision it cannot resolve is treated as another repository's commit and is not fatal. Mutation arms: dropping the digit requirement makes the word acceded, seven letters that are all hex digits, a revision, and fails 2 checks. Routing a foreign revision to MISSING fails 3 checks.

**(d) A named file that does not exist.** A file named on the command line that did not exist was silently skipped. The run printed `checked=0 missing=0` and exited 0, which is a pass over a document nothing had looked at. This was found by accident. A probe ran the checker over a documentation path that exists in cpp23-logger and not in fastestjsoninthewest. A named file that is absent is now refused by name. A file that the diff lists and that is gone is still skipped, because a branch may legitimately delete a document. Mutation arm: restoring the silent skip fails 3 checks.

**(e) The section that documents the gate had switched it off.** Found by running the gate on the very documentation
describing it, in sensen's `docs/agentic/ROADMAP_AND_TESTING.md`. That section's bucket table quotes the bold
`Design (not built)` marker in order to explain it, and the marker test read the raw line -- so documenting the marker
APPLIED it, and every citation in the section explaining the checker was reported DESIGN instead of MISSING. Inline code
spans are stripped before the test now, which fails toward fatal. Mutation arm: restoring the raw test fails 3 checks,
and the shape is the finding. The pre-existing check "a name next to a passing mention of the phrase is missing"
collapses from 2 to 0 as well, because one documented marker takes a whole section with it.

The script also lost one unreachable rule. A pattern for environment variables beginning `SENSEN_` or `SEN_` could never fire, because the generic identifier rule above it already matches those spellings.

## The review round, and the four silent clean passes it found

**AN ADVERSARIAL REVIEW THEN FOUND TEN MORE, FOUR OF THEM SILENT CLEAN PASSES, and the silent ones
are the finding.** With `color.ui=always` git put ANSI escapes ahead of every `@@`, the hunk regex
matched nothing, and the run reported a clean `checked=0 missing=0` at exit 0 over a document it had
never read. Run from a SUBDIRECTORY, `--repo .` made `git ls-files` list only that subdirectory while
the diff listed repository-root-relative paths, so every file was skipped the same way. A document
the diff lists was dropped in silence when its name was non-ASCII (`--name-only` quotes it), when it
was a `.MD` or an `.mdx` (a `*.md` pathspec selects neither), when it had no readable diff, and when
it was UNTRACKED -- which is a NEW document in a lane, the commonest case there is. And four of five
invented names in a probe passed in every repository, because the gate's own self-test SPELLS them on
purpose and the fixture is tracked code outside `docs/`: the gate was its own evidence.

Six more, all now fixed at source and each with a mutation arm: a bold `Design (not built)` in
ORDINARY PROSE exempted the whole file around it, 7 of 7 citations including the lines above it; a `#`
comment inside a fenced block counted as a heading and cut a design section short; a path written ./src/a.cpp read
as MISSING although the file exists; a path:line:col citation, a bare directory with a trailing slash and a span of digits alone were
unchecked or mis-bucketed; and a base revision that does not exist, an empty repository, a path
outside the work tree and unrelated histories all ended in a Python traceback at exit status 1 -- the
same status a legitimate MISSING uses. The self-test is 30 -> **53 checks, 0 failed**.

**TWO OF THE TEN MUTATION ARMS CAME BACK GREEN AND BOTH TIMES THE ARM WAS WRONG, NOT THE FIX.** One
mutated the diff pathspec while the case it was meant to catch arrived through the untracked sweep;
the other removed a `normpath` that the atom still applied on the way out. A mutation arm that fails
nothing has not shown the fix to be unnecessary -- it has shown the test does not cover it. Both were
rewritten until they were red, and the fixture gained a committed `.MD` and a non-ASCII filename so
the file list is exercised through the diff and not only through the untracked sweep.

## A blanket replacement falsified history, and git diff is what caught it

Updating the self-test's check count across ten documents was done with a string replacement over
whole files. `30 of 30` and `30/30` are not unique to this change: the replacement rewrote
`ComputeHeloc 27/30 -> **30/30**` in this repository's CLAUDE.md into `**53/53**`, and in sensen's
technical changelog it rewrote four historical entries, among them a `test_logic*` suite's
`30/30 ctest gates` and a subreaper probe's three runs of `30/30`. Those are measurements from other
months. Nothing in the text looked wrong afterwards -- the sentences still read perfectly.

It was found by reading `git diff -U0` and asking which hunks were NOT in the region this change
touched: sensen's diff carried hunks at lines 13447, 17800 and 17902 of a file whose only intended
edit was a prepended entry. The file was restored from HEAD and the edits re-applied to the new
entry alone, bounded by a partition on the next heading of the same level; this repository's CLAUDE.md had its
one historical figure restored by hand.

**The rule this session now follows: a replacement over a document that records history is scoped to
the region being added, and the proof is that every hunk in the diff falls inside it.** A count,
a ratio or a date is exactly the kind of string that recurs in a changelog, so the blast radius of a
blanket replacement is every measurement anyone has ever recorded.

## Gates

The script's own gate, `bash tools/test_check_docs_claims.sh`, grew from 14 checks to 53. It runs on a throwaway fixture repository, so it needs no model, no build and no network. It passes 53 of 53 with 0 failures in all six repositories.

## Measured effect on a real document

Running the checker with `--all` over one OptionsAndFuturesCalculator session log:

- before: 53 citations checked, 22 missing
- after: 65 citations checked, 12 missing, 18 revisions

Twelve more citations are actually checked. The fatal count falls from 22 to 12 because the shas move to the REVISION bucket, which is not fatal.

## Positive control

A count of zero would be indistinguishable from a checker that never looked, so each repository runs `--all` over one document that exists there. In OptionsAndFuturesCalculator, `docs/INDEX.md` gives 17 citations checked. The other repositories give: sensen `docs/INDEX.md` 572, ThinButQuickWebFramework `docs/CHANGELOG.md` 1180, mortgage-nest-egg `docs/API.md` 385, fastestjsoninthewest `QUICKSTART.md` 7, cpp23-logger `README.md` 13.

## The copy of record

The copy of record is sensen's. Nothing in a consuming repository can compare its copy with sensen's today, because the `backend/sensen` submodule pin in OptionsAndFuturesCalculator predates the script. Byte-identity across the six copies is therefore kept by hand. The self-test is the only local proof that the copy where it sits works.

## Not done, deliberately

The submodule pointers of the consuming repositories are not bumped for the new fastestjsoninthewest and cpp23-logger commits. Each consumer's pin is a separate decision, taken in that consumer.

## Commands

- `python3 tools/check_docs_claims.py`
- `python3 tools/check_docs_claims.py --base REV`
- `python3 tools/check_docs_claims.py --all FILE.md`
- `python3 tools/check_docs_claims.py --json`
- `bash tools/test_check_docs_claims.sh`

## Buckets

- MISSING: fatal, exit status 1.
- DESIGN, FRAGMENT and REVISION: reported, never fatal.

## The gate found a defect in the record of its own change

The first draft of sensen's changelog entry for this work cited only the tail of a command line -- the last two words of
`python3 tools/check_docs_claims.py --all docs/INDEX.md`, inside one span of their own. That is a piece of a command, not
a quoted message and not a code name, so the checker reported it MISSING and refused at exit status 1.
Spelling the whole command resolved it. Measured on the lines that change added: the corrected section of
`docs/agentic/ROADMAP_AND_TESTING.md` checks 23 citations with 0 missing, and the whole sensen branch 45 with 0 missing.
