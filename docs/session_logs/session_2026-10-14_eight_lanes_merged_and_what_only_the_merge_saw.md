# Session 2026-10-14 — eight lane branches merged, and three defects only the merge could see

> **On backticks in this log.** The work is in ThinButQuickWebFramework and sensen, and this file
> lives in OptionsAndFuturesCalculator. The citation gate's universe is ONE repository's tracked
> files, so a backtick here is a claim that THIS repository's code carries the name. Names belonging
> to the other repositories are therefore spelled plainly; the ones still in backticks are this
> repository's own.

Eight lane branches were sitting unmerged across two repositories with nothing pushed and no
worktree deleted: seven in ThinButQuickWebFramework and one in sensen, forty commits in all.
Each lane's own commits record review rounds and a gate on its own tree -- that is what the
commits say, and it was not re-run per lane here. What WAS run is the gate on the merged result,
and the interesting part is what only the MERGE could see.

## What was unmerged

| repo | branch | commits | what it held |
| --- | --- | --- | --- |
| sensen | `lane/gw-open-defects` | 7 | #116 round 5: three operator switches parsed once, `/v1/usage` and `/v1/whoami` routed and paged, migration 16, doc corrections |
| tbqwf | `lane/dbcore` | 11 | #110 wave 0: the driver contract, Dialect, Value, registry, password redaction |
| tbqwf | `lane/h3-followups` | 6 | #106: loss timers in the library worker, zero-length SCID, dead cached connections |
| tbqwf | `lane/http-leftovers` | 5 | #121 client: curl option refusals, ad/analytics JSON escaping, Stripe key shape |
| tbqwf | `lane/server-leftovers` | 4 | #121 server: a handler that ends its own server, SOCK_CLOEXEC policy |
| tbqwf | `lane/json-escaper-sweep` | 4 | #111: one escaper for every raw JSON string |
| tbqwf | `lane/agent-rules-split` | 1 | ai/CLAUDE.md 14,903 -> 3,271 bytes |
| tbqwf | `lane/lib-install-plan` | 2 | #109 plan document |

## Three defects the merge found

### A ratchet baseline two lanes disagreed about

The json sweep added kRemaining to source_policy_tests: a per-file count of the raw JSON
string writes that still bypass the one escaper, measured ON ITS OWN TREE. The HTTP client lane
then escaped the advertising and analytics payloads -- the very conversion kRemaining's own
comment names as its owner. The assertion is an EQUALITY, so paying the debt down broke it:
amazon 6 -> 1, google 17 -> 6, linkedin 12 -> 5, meta 19 -> 8, tiktok 24 -> 3, and amplitude,
ga4, mixpanel and plausible to 0. Both lanes were green alone; the merge was red.

The numbers are the measured ones now, and the four that reached zero are OFF the list rather
than pinned at 0 -- stronger, because an unlisted file is still scanned and one raw write in it
is a stray, which fails. Proved rather than assumed: with tiktok listed as 4 instead of 3 the
test fails, and the file was restored and its md5 compared against the original.

### A path in the agent rules that had never existed

The ai/CLAUDE.md split made the rules text a CHANGED file for the first time, and the citation
gate immediately read MISSING on simd_headers/. The directory was real once --
scripts/organize_root.sh created it and moved twelve `.hpp` files into it -- and the
consolidation into src/simd/ modules left the rule behind. A doc nobody edits is never gated,
so a fictional path can sit in the file every session loads, indefinitely.

Worse: ai/CLAUDE.md stated the same path WITHOUT BACKTICKS, which the gate's atom extraction
does not see. The gate read clean on the one file that matters most. Five sibling rules files
carried the same sentence and were swept with it. The reports, session logs and
`organize_root.sh` describe the directory that did exist and are left as the record they are.

### A submodule is invisible to the citation gate

The #109 plan cited external/fastestjsoninthewest/CMakeLists.txt and
external/cpp23-logger/CMakeLists.txt. Both files are on disk. Both read MISSING, because the
gate's universe is `git ls-files` minus `docs/` and `*.md`, and the superproject tracks a
submodule as ONE gitlink entry -- so nothing inside a submodule can ever be found. The gate's
own header anticipates it ("another repository's code names come back missing"), and a submodule
is another repository.

## The plan document, and what a gate should fail on

The plan read 402 MISSING, because a plan's nouns are what it asks to be built. Every
forward-looking section now carries the `Design (not built)` marker: 362 citations reported and
not fatal. Measurements this plan rests on is NOT marked -- it is measured, and its claims stay
fatal -- and Appendix: reproducing the measurements was promoted to a top-level section so
WAVE 3's marker cannot reach it. The 41 that remained in the measured sections were external
names, which the gate's own header says to spell without backticks: CMake API and property
names, imported target names, the install prefix's own paths, system libraries and headers, and
quoted CMake and clang output. Three quoted messages keep quotation marks so the text still
shows them as output rather than prose.

## The sensen gateway lane

Its worktree held an INTERRUPTED merge from an earlier session: MERGE_HEAD pointing at
`8a350fe0f`, a conflict in `CHANGELOG.md`, and a staged tree that included master's whole new
src/text_search/ subsystem. The conflict was two new `[Unreleased]` sections, both kept, lane
first because the file is newest-first. The submodule pin resolved to master's `6c80f73a7`, and
the submodule's WORKING TREE still sat at `b9dfa998a`: a commit takes the index, so the pin is
right, and nothing in the submodule was touched -- initialising sensen's submodules recursively
in this checkout opens a flag gate that adds `-maes -mpclmul` to the shared `std.pcm`.

### The three operator switches, closed

SENSEN_GATEWAY_STORE_ALLOW_REMOTE, `SENSEN_GATEWAY_ORG` and SENSEN_GATEWAY_STREAM were each
read two ways. One strict reader (agent/src/gateway_env.cppm) parses all three now, and the
sweep is verifiable: no `env("SENSEN_GATEWAY_{STORE_ALLOW_REMOTE,ORG,STREAM}")` and no `getenv`
of those names exists outside that module. Unset is the documented default, a recognised
spelling means exactly that, and anything else -- a set-and-empty value included -- exits
naming the variable and quoting the value.

### Two module headers describing a tree that moved on

docs/GATEWAY_OPERATIONS.md section 12 lists both, and states the principle: a stale note
recording why something is impossible is worse than no note, because it is the one a reader
trusts INSTEAD of measuring. They had stayed stale anyway.

`gateway_admit_http.cppm` said this module sits in sensen_agent and that `sensen-gateway` does
NOT call routeV1 yet. agent/CMakeLists.txt lists it in gateway_core's module file set and
agent/cli/gateway_main.cpp calls gah::routeV1. `gateway_router.cppm` now also names
isAccountRoute and accountMethodRefusal, which this lane added to that same file.

### One carried open item was stale

dns_fail/good_leaf reconciliation against `deploy/gateway/seed/sensen-org.json`: those names
appear in ZERO tracked files and there is no `deploy/gateway/seed/` directory. Closed as stale
rather than carried another session.

### Section 12 corrected itself

Section 12 lists six gateway documents and says five are materially stale. Three already carried a
banner and `MULTI_TENANT_GATEWAY.md` was corrected by the lane; the two that had nothing got one.
And the section was ITSELF too strong about the handoff note: the row said "Every path it names is
gone", and measured, the five `src/raft/*` paths are gone while agent/src/agent_cli.cppm,
src/gateway/registry_state.cppm and src/gateway/replicated_registry.cppm are present. The real
hazard is narrower and worse -- `src/raft` is named twenty times, so its conflict instruction would
RE-INTRODUCE deleted code -- and the row says that now. A corrections section that overstates
teaches its reader to discount it.

### A removal banner is the one shape that must not use backticks

Both new banners tripped the citation gate, on `src/raft` and model_not_found, in sentences
written to say those names are GONE. The backtick is this repository's claim that a name exists in
its code, so a banner asserting non-existence must spell the name plainly. The banners say why, in
line, so the next person does not re-add them.

## Beyond the merge

`utils.json_number` -- one checked reader for a JSON number going into an integral type. Call sites
wrote `static_cast<T>(value.as_number())`, which is wrong three ways, and two files in the tree had
already paid for each of them (src/auth/jwt.cppm, src/codegen/json_serializer.cppm), so the
reader's reasoning is theirs rather than new:

  * `as_number()` returns a QUIET NaN for a value that is not a number, and converting a NaN -- or
    any value outside the destination -- to an integer is UNDEFINED BEHAVIOUR. `"http1": "8080"`,
    a port written as a string, is the shape that reaches it.
  * `is_number()` is true for a float128, which `as_number()` ROUNDS.
  * Asking a double for a large integer's digits rounds, so `std::uint64_t` max does not survive.

Seven sites in src/cli/config_manager.cppm (three ports and four performance counts, none of them
guarded at all) and three in src/cli/benchmark_runner.cppm are on the reader now. A value that is
not a whole number fitting its destination keeps the documented default and says so through
print_warning rather than wrapping silently: 99999 used to become port 34463.

## The documentation citation gate now has a runner in three more repositories

It was carried by six repositories and registered with a test runner by ONE. A gate no runner calls
is indistinguishable from a gate that passes, which is the defect the gate itself exists to catch in
prose. tbqwf registers it as the ctest `check_docs_claims`; OptionsAndFuturesCalculator as the ctest
`CheckDocsClaimsGate`; nest-egg-loan as rule 28 of `scripts/code_policy_check.sh`, which
`npm run policy` runs, with config/code_policy.txt recording the rule so the script no longer
cites one the policy does not contain. Both of rule 28's failure arms were exercised rather than
assumed -- a stub that exits 1, and a missing script -- and the real script was restored and its md5
compared.

fastestjsoninthewest and cpp23-logger still have no runner for it. Neither has a standalone
checkout here; their submodule working copies sit at pins that predate the gate and do not even
carry the file.
