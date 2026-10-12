# Gateway store migrated to 13, and the submodule nobody had named

@author Olumuyiwa Oluwasanmi

Date: 2026-10-11 (evening; the morning's migration to 12 is
`session_2026-10-11_gateway_store_migrated_and_the_role_that_could_not.md`)

Owner instruction: *"migrate the store to 13 and redeploy"*, then *"did you add the necessary
regression tests?"*

## Outcome

The live gateway store is at **schema 13** and serving. Window **T0 2026-10-11T21:13:10 -> T1
21:13:25 = 15 seconds**. `admin init` returned `ready at schema version 13` (rc=0) with the owner
DSN passed as a podman secret created from stdin and removed afterwards; `redeploy.sh` then ran
rc=0: `store ok: schema_version=13 orgs=1`, readyz `ready/store_reachable/epoch_current/
admission_wired` all true, regression **20 passed 0 failed** before and after a restart that came
back in **2 s** against a 20 s budget, restart count 0, `REDEPLOY COMPLETE sensen=e8c8dfc97
tbqwf=dd94b7890`.

Rollback image id **`9a690b5b1b27`** (the `pg-tier` tag moves on every build, so the tag is not a
rollback target). Pre-migration `pg_dump -Fc` kept: `sensen_gw_pre13_20261011T211116.dump`,
198,869 bytes, rc=0.

The behaviour change is visible rather than inferred. `admin plans` now prints

    an org with no plan is on the default plan, which is not a row: plan=default ration=fair weight=1 borrow=no

where the old binary printed *"an org with no plan is rationed by its token caps alone"*. The store
has **0 plans and 0 orgs assigned** and `orgs=1`, so migration 13 did not add a latent feature
here: it changed the admission path for **every** request on this gateway.

## THE DEPLOY SOURCE HAD TO MOVE, AND THE FIRST TWO CANDIDATES COULD NOT BUILD

The morning's deploy source was pinned at `6bc66aa76` and left there on a measurement: zero
gateway source files differed from master, so building from it shipped identical gateway code.
That stopped being true. Master is now **988 commits** ahead of it with **13 gateway files**
changed, and `6bc66aa76` carries migrations 1-12 only.

Three candidates, measured rather than ranked by preference:

| tree | migration 13 | recorded tbqwf pin | builds? |
| --- | --- | --- | --- |
| `6bc66aa76` (morning's source) | NO | `dd94b7890` | yes, but no 13 |
| `27f1afbb6` (my gated fair-plan merge + docs) | yes | `a8b3f1006` | **NO** |
| `e8c8dfc97` (master) | yes | `dd94b7890` | yes |

**`27f1afbb6` cannot build its own gateway binary with its own recorded pin.**
`agent/src/gateway_tls_reload.cppm` wants `tbqwf::tls`, `Result` and
`IUnifiedServer::reload_tls`; `a8b3f1006` (2026-10-06 22:00) has none of them and `dd94b7890`
(2026-10-07 10:54) has all three, and `a8b3f1006` is an ANCESTOR of `dd94b7890`, so the recorded
pin is strictly older than the code that needs it.

**I did not regress that pin, and checking rather than assuming is the point.** Both parents of
the merge AND the merge base `4395e86d5` -- which was master at the time -- pin `a8b3f1006`. So
master itself was in a state where the gateway binary could not build; the merge carried that
faithfully, and master has since moved to `dd94b7890`.

**What my merge report DID get wrong was treating "0 conflicts" as reassurance.** The three gates
it was green on -- `test_gateway_ration` 98/0, `test_gateway_store_sqlite` 655/0,
`test_gateway_store_pg` 558/0 -- link none of `gateway_tls_reload`, so no gate in that set could
observe the pin being wrong for the deployable artefact. A gate suite that cannot exhibit a
failure is not a control for it, which this tree records three times elsewhere.

## THE BINARY WAS BUILT FROM A TREE THAT WAS NOT THE RECORDED ONE

Measured on the first build from master: the deploy source's `external/fastestjsoninthewest` was
checked out at `e8c970c` while the superproject recorded `25ffd2e65`. `redeploy.sh` verified
**`external/tbqwf` by name** and nothing else, so the binary that came out was not the recorded
tree and nothing said so -- fastjson was simply not the submodule anybody had thought to name.

`deploy/gateway/submodules_pinned_ok.sh` replaces that with a predicate DERIVED from `git
submodule status`, so a submodule added later is covered without anyone remembering. Only a
leading space is a release; `+` is refused in BOTH directions, because a submodule AHEAD of its
pin is code that was never recorded and is no safer than one behind. `-` (uninitialised) and `U`
(conflicted) are refused, and empty output is refused because the deploy source has five
submodules, so "nothing to check" means the check did not run.

## THE IMAGE DID NOT CARRY A LIBRARY THE NEW BINARY NEEDS

Master's gateway links `libsensen_gateway_decision.so.1` out of the build tree, which the context
copy did not stage, and the image died before `main` with `cannot open shared object file`. The
loader probe in section 1 caught it **before** any container was touched, which is what it is for.

The staged set is DERIVED from `ldd` rather than globbed, and that choice is load-bearing:
`build-gateway/lib` also holds `libsensen.so.1` at **215 MB**, which this binary does not link at
all, plus two test plugins. `ldd` names the 20 MB library the loader actually needs and nothing
else. A host-side `not found` is now also refused one step earlier, naming the library, because
the image's own probe costs a full build to ask.

## THE REGRESSION GAP, AND WHAT WAS AND WAS NOT MISSING

Asked directly whether the necessary regression tests existed. Measured, not asserted:

**Already covered.** Migration 13's behaviour, by the lane's gates on scratch databases, with
explicit no-plan arms (`test_gateway_store_pg.cpp:1905-1913`). And the live store's schema
version, by the binary itself: `verifyLocked` refuses a store BEHIND it
(`gateway_store_pg_impl.cpp:746`) and AHEAD of it (`:762`), so reaching section 2 at all proves
`schema_migrations` reads exactly 13. A separate schema-equality check would have been redundant,
and the first draft of this work nearly built one.

**Genuinely missing: the ARTEFACTS, as distinct from the version row.** Migration 13 grants
`select (id)` on `orgs` to `gateway_capacity`, creates the `orgs_capacity` policy, and replaces
`model_fair_tenants` so its `where` clause reads `op.org_id is null or a.rations_fair`. That
function is SECURITY DEFINER and fair rationing divides a model's `max_concurrent` across the rows
it returns -- so if the grant or the policy were missing, or were dropped later, it returns FEWER
tenants and hands every survivor a LARGER share. No error, no refusal, no log line: the gateway
over-admits and looks healthy, while `schema_migrations` goes on reading 13. Same reason the
strategy store asserts its RLS posture directly instead of through behaviour.

`deploy/gateway/fair_posture_ok.sh` asserts all three, and `redeploy.sh` runs the probe INSIDE the
database container, so it carries no DSN, no password and nothing a printed refusal could leak.

**It has a real before/after control rather than a self-confirming green.** Run against the live
store at schema 12, twenty minutes before the migration, it answered

    capacity_can_select_orgs_id=false
    orgs_capacity_policy=false
    fair_tenants_includes_planless=false   -> REFUSED

and in the successful redeploy it answered all three `true`. The schema-12 text is kept verbatim as
one of the gate's arms, so the refusing case is the one that really occurred.

Requiring the KEY as well as the value is what makes a silent probe a refusal: a query that
errored before its third statement matches two keys, and would otherwise read as a pass -- the
`schema_version=-1` lesson, which is that a surface answering "I do not know" must not be taken
for "yes".

## Gate

`scripts/check_gateway_deploy_guard.sh` **30 -> 47 passed, 0 failed**. Mutation-checked with both
predicates restored byte-identically afterwards:

| arm | fails |
| --- | --- |
| `submodules_pinned_ok` accepts everything | exactly 4 -- `+`, `-`, `U`, empty; the ADMITTED arm still passes, so the positive control is intact |
| `fair_posture_ok` reads only the first key | exactly 3 -- missing POLICY, stale `model_fair_tenants`, TRUNCATED probe |

The second arm's discriminator is the check it does NOT break: "a missing GRANT is REFUSED" still
passes, because that is the one key it still reads. An arm that fails everything proves far less
than one that fails exactly what it removed.

## The review found a gate two of whose arms a COMMENT satisfied

A Sonnet review of commit `98d6341`, run before it was pushed, returned eleven findings, eight of them real, and the verdict **"not safe to push as is"**.

**THE WORST FINDING WAS TWO VACUOUS ARMS.** `scripts/check_gateway_deploy_guard.sh` asserted that `deploy/gateway/redeploy.sh` uses each predicate by grepping for the bare name, for example `grep -q 'submodules_pinned_ok'`. That string also occurs in redeploy.sh's own line `# shellcheck source=deploy/gateway/submodules_pinned_ok.sh`, so deleting BOTH the `.` source line and the call left the gate printing ok. The posture predicate had the same defect. The arms now match the CALL shape, `grep -qE '\|[[:space:]]*predicate'`.

**THE SECOND FINDING WAS A PROBE RUN AFTER THE SERVING CONTAINERS WERE GONE.** The posture probe ran in section 3b, after `podman rm -f` had already removed the serving containers. The probe needs only the database container, never the new gateway containers, so every way it can fail (no such container, wrong database, psql cannot authenticate) was a discovery made with the serving containers already removed and no rollback available. It is section 1c now, before anything is replaced. The gate asserts that ordering by comparing line numbers, not by trusting a comment.

**THE THIRD FINDING WAS A PROBE THAT TRUSTED ITS OWN DEFAULTS.** The probe took the operator defaults `PGC` and `GWDB` to name the store the gateway opens. They cannot promise that, because the store is named by the `sgw_store_dsn` podman secret, which the script deliberately cannot read. The probe now also reports `probe_schema_version` and `probe_orgs`, and they must equal the `schema_version` and `orgs` that `admin show` read through that secret in section 1b. Before this, a probe pointed at another database answered confidently.

**Smaller fixes in the same pass:**

* `git submodule status` is taken with its EXIT STATUS. A listing cut short by a broken submodule gitdir can be all-clean and incomplete, so the number of lines it reports is compared against the count in `.gitmodules`.
* Each submodule is also compared against HEAD's tree. `git submodule status` compares to the INDEX, so a staged-but-uncommitted gitlink bump read as clean.
* The `ldd` prefix is canonicalised through `realpath`, because `SRC` may be a symlink while `ldd` prints the resolved path.
* `podman ps | grep -q` became a podman `--filter`. Under `pipefail`, grep exiting on its first match can SIGPIPE the writer and fail the pipeline.
* The pass path prints only well-formed `key=value` lines, because psql can emit NOTICE text alongside the rows.
* The log filters now refuse `postgres://` as well as `postgresql://`.

**THE NEW ORDERING ARM'S OWN FIRST RUN WAS THE DEFECT IT EXISTS FOR.** It reported `rm=154`, which is a COMMENT in section 1b that names `podman rm -f`, rather than the command itself a hundred lines further down (a line number this file deliberately does not pin, because editing the script moves it). The arm is anchored with `^[[:space:]]*` now.

**A DIRTY SUBMODULE WORKTREE IS STATED, NOT REFUSED.** A dirty submodule worktree is still invisible to the predicate, and that is deliberate. Measured, `external/tbqwf` carries a regenerated `src/bpf/steering.skel.h` on every build, so refusing a dirty submodule would refuse every deploy. The claim the block supports is "every submodule is at the commit HEAD records", not "byte-identical to the recorded tree".

The gate went from **47 passed / 0 failed** to **54 passed / 0 failed**. Both files were restored byte-identically after mutation:

| arm | fails |
| --- | --- |
| predicate CALL removed, shellcheck comment left in place | exactly 1, where the old grep passed |
| posture call moved to after container replacement | exactly the ordering arm |

The real deploy was then re-run on the edited script, and every stage passed:

    preflight ok: sensen=e8c8dfc97 tbqwf=dd94b7890 submodules=5-at-HEADs-recorded-commits
    store ok: schema_version=13 orgs=1
    fair posture ok: capacity_can_select_orgs_id=true orgs_capacity_policy=true fair_tenants_includes_planless=true probe_schema_version=13 probe_orgs=1

The regression suite was **20 passed, 0 failed** before and after a restart that returned ready in **2 s** against a **20 s** budget. Restart count was 0 and the exit code was 0.

## Still open

* **tbqwf's `build/` directory cannot compile at all, and it is not this change.** It fails on
  `/usr/local/include/c++/v1/stddef.h:39: 'stddef.h' file not found` while building oneTBB --
  libc++'s own wrapper doing `#include_next` with clang's resource dir out of order. Proven
  pre-existing by baseline: the identical objects fail the same way with the ORIGINAL fastjson pin
  restored. The per-triple `__config_site` is present, so the build dir is stale against clang
  23.1.2 rather than tbqwf being broken.
* The live store still talks to PostgreSQL in cleartext (`sslmode=disable`, loopback).
