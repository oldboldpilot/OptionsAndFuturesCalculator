# Session 2026-10-05 — the gateway lanes land on sensen master, and OFC takes the bump

@author Olumuyiwa Oluwasanmi

Asked for three things: merge everything to master under the update policy, deploy
the latest in mfv and OFC, and say what the sensen server gateway/router's status is.

## The gateway is finished as a serving component, and that is measurable

All twelve increments of `docs/agentic/MULTI_TENANT_GATEWAY.md` §14.1 are BUILT.
Verified against the running container rather than read off the table:

```
/readyz  ready:true store_reachable:true epoch_current:true
         admission_wired:true           <- the field this session's lane added
         tls.observed:X25519MLKEM768    <- the PQ hybrid group NEGOTIATED, not merely offered
         posture:pq  regulated:false
regression.sh  20 passed, 0 failed
```

`tls.observed` is the load-bearing one: §14's own mutation arm for increment 4a is
"`s_client -groups X25519MLKEM768` not reporting that group as negotiated while
/readyz lists it configured", so a configured-but-not-negotiated group is the
failure this field exists to make visible.

What is NOT finished, stated as the document states it: increment 11's plugin host
is built and gated (26 + 13 checks, real `dlopen`) and WIRED TO NOTHING on purpose
-- §15 Q10 recommends no, because a Prolog policy is ALREADY compiled to native
through `logic_emit`, so a `.so` buys no speed and costs explicability. The
PostgreSQL tier's wire transport has no TLS and so refuses a non-loopback host.
The SQLite tier cannot have RLS and `rlsEvidence()` returns a refusal saying so.
Nothing calls artefact `evict` on a schedule. Peer forwarding is first-configured
only, because a completion is not idempotent. §15 lists 22 owner decisions.

## Two lanes merged; "everything" turned out to be exactly those two

| branch | commits | result |
| --- | --- | --- |
| `lane/gateway-logger-and-refusal-statuses` | 4 | merged `f33ced55` |
| `lane/gateway-derive-probe` | 1 | merged `37da7409` |

The merge base was `67742766` -- which is exactly where OFC was pinned -- and master
had moved 172 commits since. **No conflicts, and that proves nothing**: this file
records the 2026-08-25 merge that was clean because changes touched different LINES.
So the content was checked instead. Master touched neither of the 12 gateway files
nor any submodule pin, and every lane-only file is BYTE-IDENTICAL to the lane tip.
The lane's submodule bumps (tbqwf +49, SGEE +211, nanobind) therefore carried
cleanly rather than being merge artefacts.

Swept every remote branch in all four repositories against the new master, so
"merge everything" is a measurement rather than an assumption:

- **sensen** -- only those two branches existed on either remote; both are now
  ancestors of master. Nothing unmerged.
- **mortgage-nest-egg** -- nothing unmerged, and `main` was already in sync with
  both remotes.
- **OptionsAndFuturesCalculator** -- one branch, `claude/finetune-bert-models-
  quantization-ngwmnd`, NOT merged and deliberately not merged. It is 18,582 lines
  BEHIND master, its single unique commit is docs-only, and that commit's entire
  content is already on master VERBATIM (checked by three distinct probe strings).
  It also breaks the authorship policy twice over: an AI agent name in a branch
  name, and an author email of `oluwasanmi@sensen.ai`, which is on the deny list.
  Merging it would delete working code to add nothing. It wants deleting, which is
  the owner's call on a published ref.
- **ThinButQuickWebFramework** -- one branch, `wip/quic-http3-2026-10-04`, 4 ahead
  and 4 behind, 5,519 insertions. Its own commit message says *"UNFINISHED,
  preserved off main"*. Merging a feature that declares itself unfinished is not a
  decision to take on someone's behalf.

## sensen master cannot configure on a WARM build dir, and that is master's debt

The merge's first gate failed at `CMakeLists.txt:3159` -- `clang-tidy validation
failed!`. The mechanism is worth recording because it explains how the condition
survived: the gate runs only

```cmake
if(NOT SENSEN_SKIP_TIDY AND EXISTS "${CMAKE_BINARY_DIR}/compile_commands.json"
   AND SENSEN_TIDY_MODMAPS)   # *.modmap, i.e. module BMIs already built
```

so a FRESH clone skips it (neither artefact exists yet) and only a warm directory
catches it. Measured: **346 files flagged across 59 "warnings treated as errors"
groups** -- worst `optimizer_dispatch.cppm` 1681, `rope_dispatch.cppm` 961 -- all
excessive-padding and signed-bitwise lints.

**0 of those 346 are among the 16 files this merge changed.** That was checked with
sorted `comm` and a positive control proving the comparison would have found an
overlap, after a first attempt ran `comm` on unsorted input and reported 0
meaninglessly. So the debt is master's own and the merge is exonerated; the gate was
bypassed through the mechanism `CMakeLists.txt:3115` documents rather than by
absorbing 346 files of unrelated change.

**A grep lesson alongside it.** The authorship check first reported two VIOLATIONS,
both of which were the string `CLAUDE.md` in a commit body. `grep -i claude` cannot
tell an attribution from a citation. Re-run against attribution SHAPES
(`Co-Authored-By:`, `@author`, `Claude-Session:`, `generated with`) with a positive
control proving the pattern fires on a planted trailer: clean, and all seven commits
are `Olumuyiwa Oluwasanmi <muyiwamc2@gmail.com>`.

## Gates

**sensen, gateway configuration** (`SENSEN_TBQWF=ON`, the gateway needs TBQWF's HTTP
client and HMAC), `CCACHE_DISABLE=1` because 172 commits of module interface moved:

```
configure   rc=0, 0 CMake errors
            SIMD floor: +aes +pclmul (embedded TBQWF consumes the shared std.pcm)
            sensen: building `logger` from external/cpp23-logger        <- fix 1
            TBQWF: `fastjson` already defined by the enclosing project  <- fix 2
            SGEE: logger BMIs -> .../CMakeFiles/logger.dir (target logger)  <- fix 3
build       rc=0, 1674 steps, 0 anchored `file:line:col: error:` (control fires)
suites      test_gateway_decision 81/0   test_gateway_admit      52/0
            test_gateway_router  132/0   test_gateway_admit_http 131/0
logger gate logger.code_generator.pcm 14,553,164 B + logger.pcm 19,470,384 B
            check_logger_bmi.sh rc=0; self-test 3 arms OK
probe       gateway_derive_probe derives: truncated=0 disputed=0, CONTAINS permitted/2
```

All four figures match the pre-merge lane exactly, which is what makes them
comparable rather than merely green.

**The four fixes are confirmed by the configure log naming each branch it took**,
not by the build succeeding -- the 2026-10-05 defect was precisely a build that
succeeded while a stub logger won.

**OFC, embedded configuration** -- the one nobody upstream compiles, which is where
a submodule bump has to be built (the `CMAKE_SOURCE_DIR` lesson):

```
closure check   exactly the four documented false positives, NO new entries
configure       rc=0, 0 CMake errors
                SIMD floor: no -maes/-mpclmul (no embedded TBQWF)
                SGEE: logger BMIs -> .../CMakeFiles/sgee_logger.dir, DERIVED from
                its BINARY_DIR -- resolving to sgee_logger because
                CPP23_LOGGER_TARGET_PREFIX makes `logger` an ALIAS
engine          rc=0, 468 steps, 0 anchored errors, relinked 13:14
build_tests     rc=0, 955 steps, 0 anchored errors
ctest           213 tests, 206 passed, 0 FAILED, 7 skipped   <- baseline was 213/7
smoke finance   rc=0, independent identities satisfied
smoke options   rc=0, PRO_GATE_MODE=off so the MULTI-LEG path was exercised
boot            SIMD: runtime tier avx512 ... compiled floor x86-64-v3
                Built by: clang 23.1.2, libc++ _LIBCPP_VERSION=230102, C++23
                /proc/<pid>/exe carries NO `(deleted)` suffix
```

**213/7 before and 213/7 after is what makes +0 gained / +0 lost attributable**, and
the skip LIST is what makes it readable: all seven are absent brokers, a missing GPU
collective, or the sanitizer overlay. One composition change a count could not see --
`TbbInstrumentationMatchesSanitizer` now PASSES and `CausalHookMissingFailsByName`
skips, so the set has the same size and different members.

## `smoke_client` WAS STALE, and `ninja build_tests && ctest` cannot see that

Its binary was dated **Oct 4 18:45**, four hours before the engine relinked. The
cause is in `backend/CMakeLists.txt:1859`: `build_tests` sweeps `BUILDSYSTEM_TARGETS`
matching `^test_`, and `smoke_client` is deliberately not named that way. So the
documented pair `ninja -C backend/build build_tests && ctest` passes beside a
smoke client built against a different sensen -- the `ninja && ctest` stale-binary
trap this file already records, in a target the trap's own description does not
name. Built BY NAME (`ninja -C backend/build smoke_client`, 3 steps, relink only)
and re-run: rc=0 on both suites.

**ONE ninja per build directory.** The rebuild had to wait for `ctest` to release
the directory; two ninjas in one build dir corrupt BMIs and the tell is clang
crashing on a DIFFERENT file each run.

## An ERROR count I reported wrongly, and what it actually is

The gate printed `ERROR: 0 WARN: 0` and that was quoted as a clean boot. It is the
**boot-time** count only -- the script measured it before the smoke suites ran. The
honest figure is **0 at boot and 2 during smoke**, identical in both runs:

```
HTTP 404 from https://data.alpaca.markets/v2/stocks/NQ/snapshot
    {"message":"no snapshot found for NQ"}
Quote unavailable for NQ: Market data provider returned an error status
```

Alpaca serves no snapshot for the bare futures ROOT `NQ`, which is the futures-root
problem this project already knows about from the other direction (`ES` is both the
E-mini root and Eversource Energy). `smoke_client` still returns rc=0, because
declining a quote it cannot get is the correct behaviour. Environmental, unchanged
across runs, and not a regression -- but *"0 errors"* was the wrong sentence, and
the reason is that a count is only true of the window it was taken in.

## Pre-deploy baselines, taken before production was touched

```
mfv contract suite    17/17  (5 assistant-contract + 12 closing-costs) vs the OLD engine
production payment    -3210.56057801266526493048779502036505463691
                      == the 38 places CLAUDE.md records
```

**That 17/17 was nearly vacuous and the check is the lesson.** Both files guard on
`RUN_CONTRACT_TESTS === "1" && !!FINANCE_API_KEY`, and `FINANCE_API_KEY` is in none
of `.env`, `.env.local`, `config/.env` or the shell -- so the obvious reading was
that the suite had skipped and reported nothing. It had not: `vitest.setup.ts` loads
`.env` **and `.env.development`**, and the key is in the latter. Confirmed by
running `--reporter=verbose` and reading per-test durations (184 ms, 79 ms, 29 ms)
that only a network round trip explains. A suite that skips reports `17 skipped`,
which is exactly what the no-flag arm printed -- so the two arms together are the
control.

The reason this baseline is required at all is recorded in this file already: the
18->38 scale cutover turned `closing-costs-contract.test.ts` red because the engine
became MORE accurate, and only a pre-deploy run made that attributable.

## A positive control that failed at the HTTP layer and worked at the gRPC one

Probing a nonexistent RPC as a control returned **HTTP 200**, which makes an HTTP
code useless as a discriminator on this ingress. The truth is one layer down:

```
POST /sensen.finance.Finance/__nope   ->  HTTP/2 200
                                          content-type: application/grpc
                                          grpc-status: 2
                                          grpc-message: Missing :te header
                                          body: empty
```

against the valid RPC's JSON body with a value. So the control discriminates, read
at the right layer -- the same shape as `last_applied`, the LIVE badge and Railway's
SUCCESS, which this file records three times over.
