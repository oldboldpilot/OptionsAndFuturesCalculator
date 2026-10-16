# Session 2026-10-16 — the recorded submodule pin could not build its own consumers

@author Olumuyiwa Oluwasanmi

The work was in sensen. Nothing in this repository changed except this log: the sensen submodule pointer here still sits
at its own pin, deliberately, because moving it implies a C++23 engine rebuild and a production deploy.

Paths under the sensen submodule are written WITHOUT backticks throughout. This repository's documentation citation gate
resolves a backticked name against this repository's own file list, which sees backend/sensen as a single gitlink entry,
so a backticked submodule path would be reported MISSING.

## The question that was asked, and the answer that was not in the plan

The question was whether the sensen gateway and router are done. They are: increments 1 through 12 of the gateway plan
are built, the gateway serves authenticated requests from its own store, and 69 registered gateway tests pass. The plan
was the wrong place to look for that answer, so it was read out of the code instead, and three cells of the plan's own
state table turned out to be stale — two of them understating what ships.

Closing the two limits those cells named is what found something no cell could express: sensen master could not be
built from a clean checkout at all. The gateway's TBQWF-facing module consumes a typed transport error that did not
exist at the commit the external/tbqwf submodule pointer recorded. The pin was 314 commits behind the code depending on
it. It survived because every developer tree built — each had the submodule fetched ahead of the pin for some unrelated
reason, so the recorded commit was the one version nobody's build used.

## What a submodule pin is, and why nothing caught this

A submodule pointer is the only place a dependency's version is stated, and it is the one statement that no build on a
working tree ever reads. That is the whole defect class. A new gate in sensen now checks that every name the tree spells
from a submodule exists at the commit the index records, and it reads the index rather than the submodule's worktree
precisely because reading the worktree reports a clean tree on the trees where the defect hides. Sweeping the class
found seven absent names where the gateway failure had exposed two: the TLS-reload module was in the same state.

The same lesson arrived twice more in the same day. The lane pre-check tool died outright on this lane, because it loads
each touched path's text with a git show and a changed submodule pointer is not a blob; this was the first lane ever to
move a pin, so nothing had hit it. And the code-facts indexer reports the same thing more gently, declining to record
currency evidence for a pointer whose bytes it cannot read. One class, three tools, two of them now fixed.

## The two limits, and a review that found what the gates could not

The first limit was that the vectors from RFC 4231 had never been run against the HMAC that signs admission tickets,
while a comment in the code claimed a gate checked exactly that. Both were wrong in different directions: the test that
named the standard was comparing the standard against its own second implementation, which measures the test. Six
vectors now run through both production seams. The first run failed fourteen checks, and the fault was in the test — the
crypto backend was never initialised, the production HMAC returns zeros when it cannot be created, and a signer and a
verifier agreeing on zeros agree with each other and with no standard.

The second limit was that the gateway's advertised identity had to match its own certificate and nothing checked it. It
does now, at boot, fatally where a peer list makes the name load-bearing and as a printed note otherwise.

Then an independent review found the one defect the lane's own gates could not see, and it was in that new check: it
compared the configured identity byte-for-byte against a lower-cased certificate name, so a correct deployment whose
identity differed only in letter case was refused at boot — while every peer would have accepted the hop, because the
receiver folds case before comparing. The check had reintroduced, pointing the other way, the exact failure it exists to
prevent. The tree had already learned that lesson and written it down beside the receiver's own predicate, citing
RFC 4343. The fix calls that predicate instead of restating it, on the principle that two implementations of one
comparison cannot disagree if there is only one. Every check that existed used a lower-case identifier, which is why the
gate was silent.

## State

- sensen master carries the work, pushed to origin and gitea and confirmed by ls-remote, with the changed files synced
  to the NAS and md5-matched.
- This repository: this log only. The sensen pointer here is unchanged.
- Still open in sensen, and named in the completion record with the decision each needs: scheduled artefact eviction has
  no operational entry point at all, and wiring one needs a decision about where its byte cap comes from; per-org layer
  text from activations; and the hybrid group has never been observed negotiated on an outbound hop. The peer choice
  having no failover is a decision rather than a gap — a completion is not idempotent, so retrying one is unsafe.
- For the owner: moving a submodule 314 commits changes what the gateway links, so whether that ships as its own release
  is a call this work recorded rather than made.
