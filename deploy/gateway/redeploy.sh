#!/usr/bin/env bash
#
# Re-image the PostgreSQL gateway tier and prove the new binary serves it.
#
# @author Olumuyiwa Oluwasanmi
#
# THIS IS NOT A CUTOVER, and the difference is the reason it exists. The
# gw_cutover*.sh scripts move the rig from the SQLite container to the
# PostgreSQL pair ONCE, and their failure path starts the SQLite container back
# up. That path is spent: the pg tier is already serving and the SQLite
# container is a cold rollback copy. What every later sensen gateway fix needs
# is this instead -- rebuild the image, replace the two containers, prove it.
# Re-running a one-way cutover to redeploy would stop a container that is
# already exited and roll back to a store that is no longer the system of
# record.
#
# THE GATE IS A RESTART, NOT A COLD /readyz, AND A COLD CHECK WOULD HAVE PASSED
# THE DEFECT THIS WAS WRITTEN FOR. sensen 64c3e1f5f fixed the gateway
# restart-looping twenty times on "port in use" while nothing listened: TBQWF
# passed SO_REUSEADDR | SO_REUSEPORT as ONE option NAME (2|15 == 15), and the
# bind probe passed no options at all, on the false premise that a plain bind
# tolerates TIME_WAIT. A container that has served NO traffic holds no socket
# in TIME_WAIT, so it answers /readyz on the broken image exactly as it does on
# the fixed one. `ss -ltn` being clean is not "port bindable" either. The
# sequence is therefore: deploy -> regression -> restart IMMEDIATELY -> require
# /readyz back inside RESTART_BUDGET -> regression again.
#
# IT DOES NOT ROLL BACK, deliberately. The PostgreSQL store is the system of
# record; starting the cold SQLite container on a failure here would serve
# older data and call it success. A failure leaves the containers exactly as
# they are and says so, because the next step is diagnosis and not traffic.
#
# Secrets travel as podman secrets and are never printed. Do not add a step
# that dumps container environment: those values include multi-line PEM, which
# line-based redaction leaks.
set -uo pipefail

SRC="${SRC:-/home/muyiwa/.cache/sensen-gwpg}"
OFC="${OFC:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
CFG="${CFG:-$HOME/.config/sensen-gateway}"
IMG="${IMG:-localhost/sensen-gateway:pg-tier}"
PORT="${PORT:-9443}"
BASE="https://127.0.0.1:${PORT}"
BIN="$SRC/build-gateway/bin/sensen-gateway"
# The stall it exists to catch ran up to 60 s. 20 s is comfortably above a
# healthy restart and comfortably below the defect, so it discriminates.
RESTART_BUDGET="${RESTART_BUDGET:-20}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

die(){ echo "FAIL: $*" >&2; exit 1; }
say(){ printf '%s\n' "$*"; }
ready(){ curl -sk -m3 "$BASE/readyz" 2>/dev/null | grep -q '"ready":true'; }
wait_ready(){ for _ in $(seq 1 "$1"); do ready && return 0; sleep 1; done; return 1; }

# --- 0. preflight: the code was merged, and the binary is it, not stale ------
# The code being deployed must be code that was merged. `SRC` defaults to a
# worktree, so without this a default deploy ships whatever lane somebody last
# checked out there and prints its SHA as though it had been released. The guard
# lives in its own script because it is the one preflight step that can be
# exercised on throwaway trees, and so has a gate of its own
# (`scripts/check_gateway_deploy_guard.sh`); its header states what a cached
# remote-tracking ref can and cannot tell you.
"$OFC/deploy/gateway/require_merged.sh" "$SRC" || exit 1
[ -x "$BIN" ] || die "no binary at $BIN -- build it first (ninja -C $SRC/build-gateway sensen-gateway)"
git -C "$SRC" diff --quiet HEAD -- agent/ || die "$SRC agent/ has uncommitted changes"
# General staleness, not one representative file: ANY agent/ source newer than
# the binary means the binary does not contain it.
STALE="$(find "$SRC/agent" -type f -newer "$BIN" -print -quit 2>/dev/null)"
[ -z "$STALE" ] || die "binary is older than $STALE -- rebuild"
SC="$(git -C "$SRC" rev-parse --short=9 HEAD)"
TC="$(git -C "$SRC/external/tbqwf" rev-parse --short=9 HEAD)"
# EVERY submodule must be at its recorded commit, not just the one somebody named. This checked
# `external/tbqwf` alone until 2026-10-11, and that day the deploy source built with
# `external/fastestjsoninthewest` at `e8c970c` against a recorded `25ffd2e6` -- a binary that was
# not the recorded tree, with nothing to say so, because fastjson was not the submodule the check
# names. The predicate is DERIVED from `git submodule status` so a submodule added later is
# covered without anyone remembering to add it, and it lives in its own file so it can be fed
# controls (`scripts/check_gateway_deploy_guard.sh`).
# shellcheck source=deploy/gateway/submodules_pinned_ok.sh
. "$OFC/deploy/gateway/submodules_pinned_ok.sh"
# THE EXIT STATUS IS CHECKED, not just the text. `git submodule status` that aborts part way
# through -- a broken submodule gitdir will do it -- prints the lines it managed and exits
# non-zero, and every line it managed may be a clean ' '. Reading only the text would then pass
# with the rest of the submodules unexamined, which is a silent version of the defect this block
# exists to catch.
SUBS="$(git -C "$SRC" submodule status)" || die "could not read the submodule status of $SRC, so the recorded tree cannot be established"
printf '%s\n' "$SUBS" | submodules_pinned_ok \
  || die "a submodule of $SRC is not at its recorded pin, so this binary would not be the recorded tree. Run: git -C $SRC submodule update --init
    offending: $(printf '%s\n' "$SUBS" | submodules_unpinned_lines | awk '{print $2}' | tr '\n' ' ')"
# AND EVERY DECLARED SUBMODULE MUST HAVE BEEN REPORTED ON. The count comes from `.gitmodules`,
# so a listing cut short agrees with neither it nor itself.
NDECL="$(git -C "$SRC" config -f .gitmodules --get-regexp '^submodule\..*\.path$' 2>/dev/null | wc -l)"
NSEEN="$(printf '%s\n' "$SUBS" | grep -c .)"
[ "$NDECL" = "$NSEEN" ] || die "$SRC declares $NDECL submodules and the status reported $NSEEN, so some were not examined"
# AND THE COMPARISON MUST BE AGAINST HEAD, WHICH IS WHAT A RELEASE MEANS. `git submodule status`
# compares the checkout to the INDEX, so a gitlink bump that is staged but not committed reads as
# a clean ' ' -- and `git diff --quiet HEAD -- agent/` above only covers `agent/`. This asks
# HEAD's own tree, which is what the single-submodule check it replaced did.
while read -r _ sp _; do
  [ -n "${sp:-}" ] || continue
  SA="$(git -C "$SRC/$sp" rev-parse HEAD 2>/dev/null)" || die "submodule $sp has no checkout to compare"
  SR="$(git -C "$SRC" ls-tree HEAD "$sp" | awk '{print $3}')"
  [ "$SA" = "$SR" ] || die "submodule $sp is at ${SA:0:9} but HEAD records ${SR:0:9}, so this binary would not be the recorded tree"
done <<< "$SUBS"
# WHAT THIS STILL DOES NOT SEE, stated rather than implied: a DIRTY submodule worktree. `git
# submodule status` reports the gitlink, so modified or untracked files inside a submodule still
# read ' '. It is deliberately not refused, because measured on this deploy source
# `external/tbqwf` carries a regenerated `src/bpf/steering.skel.h` on every build -- the build
# produces it -- so refusing a dirty submodule would refuse every deploy. The claim this block
# supports is therefore "every submodule is at the COMMIT HEAD records", not "byte-identical to
# the recorded tree".
say "preflight ok: sensen=$SC tbqwf=$TC submodules=$NSEEN-at-HEADs-recorded-commits"

# --- 1. image, and prove the loader runs as the unprivileged uid --------------
CTX="$WORK/ctx"; mkdir -p "$CTX/runtime"
cp "$OFC/deploy/gateway/Dockerfile" "$OFC/deploy/gateway/entrypoint.sh" "$BIN" "$CTX/"
cp -L /usr/local/lib/x86_64-unknown-linux-gnu/{libc++.so.1,libc++abi.so.1,libunwind.so.1} "$CTX/runtime/"
# THE BUILD TREE'S OWN SHARED OBJECTS TRAVEL WITH THE BINARY, and the list is DERIVED from `ldd`
# rather than globbed or written down. On 2026-10-11 the gateway began linking
# `libsensen_gateway_decision.so.1`, which this step did not ship, and the image died before main
# with "cannot open shared object file" -- caught by the loader probe below, but only after a
# build. Deriving it covers the next library nobody remembers to add.
#
# A GLOB WOULD SHIP THE WRONG SET, measured: `build-gateway/lib` also holds `libsensen.so.1` at
# 215 MB, which this binary does not link at all, and two test plugins. `ldd` names the 20 MB one
# the loader actually needs and nothing else. /opt/sensen-runtime is already on LD_LIBRARY_PATH.
# The prefix is CANONICALISED on both sides: `SRC` may be a symlink or a non-canonical path while
# `ldd` prints the resolved one, and a prefix match between the two spellings would silently stage
# nothing. Missing a library is caught by the loader probe below, but only after a full image build.
SRCR="$(realpath "$SRC" 2>/dev/null || printf '%s' "$SRC")"
mapfile -t BUILDLIBS < <(ldd "$BIN" 2>/dev/null | awk '{print $3}' | while read -r L; do
  [ -n "${L:-}" ] && [ -e "${L:-/nonexistent}" ] || continue
  case "$(realpath "$L" 2>/dev/null)" in "$SRCR"/*) printf '%s\n' "$L";; esac
done)
for l in ${BUILDLIBS+"${BUILDLIBS[@]}"}; do cp -L "$l" "$CTX/runtime/" || die "could not stage $l"; done
# A HOST-SIDE `not found` IS THE SAME DEFECT ONE STEP EARLIER. The image's own loader probe is
# the authority, but it costs a full image build to ask; this names the library for free.
NF="$(ldd "$BIN" 2>/dev/null | awk '/not found/{print $1}' | tr '\n' ' ')"
[ -z "$NF" ] || die "the binary has unresolved shared objects on the build host, so the image cannot load it either: $NF"
say "runtime libs: $(ls "$CTX/runtime" | tr '\n' ' ')"
SHA="$(sha256sum "$BIN" | cut -d' ' -f1)"
podman build -q --build-arg GATEWAY_SHA256="$SHA" --build-arg SENSEN_COMMIT="$SC" \
  --build-arg TBQWF_COMMIT="$TC" -t "$IMG" "$CTX" > "$WORK/img.log" 2>&1 \
  || die "image build failed: $(tail -3 "$WORK/img.log")"
LOAD="$(podman run --rm --entrypoint /usr/local/bin/sensen-gateway "$IMG" admin 2>&1 | head -1)"
case "$LOAD" in *"shared object"*) die "loader failed in image: $LOAD";; esac
say "image ok sha=${SHA:0:16} uid=$(podman run --rm --entrypoint id "$IMG" -u)"

# --- 1b. THE STORE MUST BE MIGRATED TO WHAT THIS BINARY EXPECTS -------------
# THE CONTAINER NEVER MIGRATES, deliberately -- `main`'s store selector says so in as many
# words, because a typo must be a refusal rather than an empty database. So a binary carrying a
# migration the store has not applied cannot start at all: `GatewayStore::open` calls
# `verifyLocked` on EVERY open and refuses a version mismatch in either direction with
# `NotMigrated`. Without this step that refusal arrives at section 2, AFTER `podman rm -f` has
# already removed the serving containers -- so the gateway would be down until an operator
# migrated, and the only sign would be a readiness timeout.
#
# IT IS MEASURED, NOT ASSUMED: this was a live state. On 2026-10-11 the store was at schema
# version 7 (migrations 1-7) while the merged binary carried 12, because the serving image was
# built on 2026-10-06 from a commit that predates migrations 8-11 and nobody had redeployed. A
# redeploy on that day would have taken the gateway down rather than updating it.
#
# THE CHECK IS THE GATEWAY'S OWN `open`, run with the gateway's own credential and scope, so it
# cannot disagree with what section 2 is about to do. `admin show` is read-only -- it prints the
# schema version, the org count and the break-glass posture and writes nothing -- and it opens
# WITHOUT migrating, which is the whole point.
#
# IT REFUSES AND NAMES THE STEP rather than migrating. Applying pending migrations to the system
# of record is an operator's decision with its own evidence, not a side effect of a redeploy: at
# the time of writing the pending set was four feature migrations and one that rewrites an audit
# control, which is not something a deploy script should do while nobody is reading.
SHOW="$(podman run --rm --network host   -e SENSEN_GATEWAY_ORG=sensen   --secret sgw_store_dsn,type=env,target=SENSEN_GATEWAY_STORE   --entrypoint /usr/local/bin/sensen-gateway "$IMG" admin show env:SENSEN_GATEWAY_STORE 2>&1)"
# THE VERDICT IS A SOURCED PREDICATE, NOT AN INLINE `case`, so that it can be GATED:
# `store_version_ok.sh` carries the rule and the reason, and
# `scripts/check_gateway_deploy_guard.sh` feeds it the real success line and three real refusals.
# The first version of the rule, written inline here, matched one of the refusals it exists to
# catch -- which is what a predicate with no positive control does.
# shellcheck source=deploy/gateway/store_version_ok.sh
. "$OFC/deploy/gateway/store_version_ok.sh"
if printf '%s' "$SHOW" | store_version_ok; then
  say "store ok: $(printf '%s' "$SHOW" | grep -oE 'schema_version=[0-9-]+ orgs=[0-9-]+' | head -1)"
else
  # The DSN never appears here: `admin show` takes it as `env:NAME` so it is not an argument,
  # and only the lines that do not carry a scheme are printed.
  # MIGRATING NEEDS A ROLE THAT OWNS THE SCHEMA'S OBJECTS, NOT THE MAINT ROLE, and this
  # message named the maint DSN until 2026-10-11 -- an instruction that cannot succeed.
  # Measured against the live store that day, with the maint role `gw_maint`:
  #   * `admin init` opens `migrate()` with `create schema if not exists <schema>`, and
  #     PostgreSQL checks CREATE on the DATABASE *before* `if not exists` checks the schema.
  #     So the statement refuses for a role without that privilege EVEN THOUGH the schema is
  #     already there -- `permission denied for database`, which reads as a missing grant on
  #     the store rather than as a no-op demanding a privilege it does not need.
  #   * past that, the pending migrations `alter table` existing tables, and ALTER requires
  #     OWNING them.
  #   `gw_maint` had USAGE on the schema, no CREATE on the database or the schema, and was a
  #   member of `gateway_maintainer` and not of the objects' owner -- so it failed at the
  #   first statement and would have failed at the next. Least privilege is correct for
  #   SERVING; applying a migration is an owner action and the instruction has to say so.
  die "this image cannot OPEN the live store, so starting it would replace a serving gateway with one that refuses to boot. Migrate first -- with a DSN whose role OWNS the schema's objects, because \`admin init\` needs CREATE on the database and ALTER on existing tables and the maint role has neither -- and then re-run this script:
      podman secret create sgw_owner_dsn -   # paste the owner DSN; never pass it as an argument
      podman run --rm --network host --secret sgw_owner_dsn,type=env,target=SGW_OWNER_DSN \\
        --entrypoint /usr/local/bin/sensen-gateway $IMG admin init env:SGW_OWNER_DSN
      podman secret rm sgw_owner_dsn
    the store said: $(printf '%s' "$SHOW" | grep -viE 'postgres(ql)?://|password=|dbname=' | tail -2 | tr '\n' ' ')"
fi

# --- 1c. MIGRATION 13'S ARTEFACTS ARE IN FORCE, NOT MERELY RECORDED ---------
# IT RUNS HERE, BEFORE ANY CONTAINER IS TOUCHED, and that placement is the point. The probe needs
# only the database, never the new gateway containers, and it was briefly in section 3b -- after
# `podman rm -f` -- where every way it can fail (no such container, wrong database, psql cannot
# authenticate) was a discovery made with the serving containers already gone and no rollback.
# A precondition belongs with the other preconditions.
#
# WHAT IT ADDS OVER SECTION 1b. 1b proves the store's VERSION, because the binary refuses a store
# behind it or ahead of it, so reaching this line means `schema_migrations` reads what the binary
# knows. It says nothing about the OBJECTS a migration installed, and the version row keeps
# reading 13 after one of them is dropped.
#
# The failure it catches is silent and its wrong answer is plausible.
# `<schema>.model_fair_tenants` is SECURITY DEFINER and returns one row per tenant competing for a
# model's capacity; fair rationing divides `max_concurrent` across those rows. Cut the
# `orgs_capacity` policy or the `select (id)` grant to `gateway_capacity` and the function returns
# FEWER tenants -- so each one is handed a LARGER share. No error, no refusal, no log line: the
# gateway over-admits and looks healthy. This is the same reason the strategy store asserts its
# RLS posture directly rather than through behaviour.
#
# THE PROBE CARRIES NO CREDENTIAL. It runs inside the database container, so there is no DSN, no
# password and nothing a printed line could leak; the gateway's own store secret stays untouched.
PGC="${PGC:-sensen-postgres}"; GWDB="${GWDB:-sensen_gw}"; GWSCHEMA="${GWSCHEMA:-sensen_gateway}"
# No pipe into `grep -q`: under `pipefail` a `grep` that exits on its first match can SIGPIPE the
# writer and fail the pipeline, which would be a spurious refusal. The filter does it in podman.
[ -n "$(podman ps --filter "name=^${PGC}$" --format '{{.Names}}')" ] \
  || die "no running container named '$PGC' to probe the store posture in -- set PGC to the database container"
# shellcheck source=deploy/gateway/fair_posture_ok.sh
. "$OFC/deploy/gateway/fair_posture_ok.sh"
POSTURE="$(podman exec -i "$PGC" psql -U postgres -d "$GWDB" -At <<SQL 2>&1
select 'capacity_can_select_orgs_id=' || coalesce(has_column_privilege('gateway_capacity','$GWSCHEMA.orgs','id','select')::text,'false');
select 'orgs_capacity_policy=' || exists(select 1 from pg_policies where schemaname='$GWSCHEMA' and tablename='orgs' and policyname='orgs_capacity')::text;
select 'fair_tenants_includes_planless=' || coalesce((select (position('op.org_id is null' in pg_get_functiondef(p.oid))>0)::text from pg_proc p join pg_namespace n on n.oid=p.pronamespace where n.nspname='$GWSCHEMA' and p.proname='model_fair_tenants' limit 1),'false');
select 'probe_schema_version=' || coalesce((select max(version)::text from $GWSCHEMA.schema_migrations),'none');
select 'probe_orgs=' || (select count(*)::text from $GWSCHEMA.orgs);
SQL
)"
# THE PROBE MUST HAVE LOOKED AT THE STORE THIS GATEWAY OPENS, and `PGC`/`GWDB` are operator
# defaults that cannot promise that: the store is named by the `sgw_store_dsn` SECRET, which this
# script deliberately cannot read. So the two are RECONCILED instead of assumed -- the schema
# version and org count the probe read must equal the ones `admin show` reported through that
# secret in 1b. A probe pointed at a different database answers about the wrong store, and
# before this it would have answered confidently.
PSV="$(printf '%s\n' "$POSTURE" | sed -n 's/^probe_schema_version=//p')"
PSO="$(printf '%s\n' "$POSTURE" | sed -n 's/^probe_orgs=//p')"
SSV="$(printf '%s' "$SHOW" | grep -oE 'schema_version=[0-9-]+' | head -1 | cut -d= -f2)"
SSO="$(printf '%s' "$SHOW" | grep -oE 'orgs=[0-9-]+' | head -1 | cut -d= -f2)"
[ -n "$PSV" ] && [ -n "$SSV" ] && [ "$PSV" = "$SSV" ] && [ "$PSO" = "$SSO" ] \
  || die "the posture probe did not look at the store this gateway opens: the probe read schema_version=${PSV:-?} orgs=${PSO:-?} from '$GWDB' in '$PGC', and the gateway's own store reported schema_version=${SSV:-?} orgs=${SSO:-?}. Set PGC/GWDB/GWSCHEMA to the database the store DSN names."
printf '%s\n' "$POSTURE" | fair_posture_ok \
  || die "migration 13's artefacts are NOT in force on the store, so fair rationing would divide a model's capacity across too few tenants and over-admit every one of them. The probe said: $(printf '%s' "$POSTURE" | grep -E '^[a-z_]+=[A-Za-z0-9]+$' | tr '\n' ' ')"
# Only well-formed `key=value` lines are echoed. psql can emit NOTICE or WARNING text alongside
# the rows, and the pass path has no business printing whatever else arrived.
say "fair posture ok: $(printf '%s' "$POSTURE" | grep -E '^[a-z_]+=[A-Za-z0-9]+$' | tr '\n' ' ')"

# --- 2. replace the two containers -------------------------------------------
GK="$(cat "$CFG/regress.key" 2>/dev/null)" || true
[ -n "${GK:-}" ] || die "no regression key at $CFG/regress.key"
podman rm -f sensen-gw-pg sensen-gw-reaper-pg >/dev/null 2>&1
podman run -d --name sensen-gw-pg --network host --restart unless-stopped \
  -e SENSEN_GATEWAY_PORT="$PORT" -e SENSEN_GATEWAY_ORG=sensen \
  --secret sgw_tls_cert,type=env,target=SENSEN_GATEWAY_CERT_PEM \
  --secret sgw_tls_key,type=env,target=SENSEN_GATEWAY_KEY_PEM \
  --secret sgw_ticket_key,type=env,target=SENSEN_GATEWAY_TICKET_KEY_HEX \
  --secret sgw_store_dsn,type=env,target=SENSEN_GATEWAY_STORE \
  "$IMG" >/dev/null || die "gateway container did not start"
podman run -d --name sensen-gw-reaper-pg --network host --restart unless-stopped \
  -e OPENSSL_CONF=/dev/null \
  --secret sgw_maint_dsn,type=env,target=SGW_MAINT_DSN \
  --entrypoint /usr/local/bin/sensen-gateway \
  "$IMG" admin reap-loop env:SGW_MAINT_DSN >/dev/null || die "reaper container did not start"
wait_ready 60 || die "not ready in 60s: $(podman logs sensen-gw-pg 2>&1 | grep -viE 'postgres(ql)?://|password' | tail -2 | tr '\n' ' ')"
say "readyz: $(curl -sk -m3 "$BASE/readyz" | grep -oE '"(ready|store_reachable|epoch_id|epoch_current|admission_wired)":[^,]*' | tr '\n' ' ')"

# --- 3. regression, which is also what puts a socket into TIME_WAIT ----------
bash "$OFC/deploy/gateway/regression.sh" "$BASE" "$GK" > "$WORK/regress.txt" 2>&1; rc=$?
say "regression rc=$rc $(grep -oE '[0-9]+ passed, [0-9]+ failed' "$WORK/regress.txt" | tail -1)"
[ $rc = 0 ] || { grep -E 'FAIL' "$WORK/regress.txt" | grep -v "$GK" | head -5; die "regression failed"; }

sleep 3
RL="$(podman logs sensen-gw-reaper-pg 2>&1 | grep -viE 'postgres(ql)?://|password')"
printf '%s\n' "$RL" | grep -qE 'reaper: tick 1 outcome=(not_due|reaped) .*orgs_without_retention=0' \
  || die "reaper did not tick healthily: $(printf '%s' "$RL" | tail -2 | tr '\n' ' ')"
say "reaper ok: $(printf '%s' "$RL" | tail -1 | cut -c1-110)"

# --- 4. THE GATE: restart with sockets in TIME_WAIT -------------------------
say "restart test: budget ${RESTART_BUDGET}s"
T0=$SECONDS
podman restart sensen-gw-pg >/dev/null || die "restart command failed"
wait_ready "$RESTART_BUDGET" || die "NOT ready ${RESTART_BUDGET}s after a restart that followed live traffic -- the TIME_WAIT bind stall is present: $(podman logs --tail 5 sensen-gw-pg 2>&1 | grep -viE 'postgres(ql)?://|password' | tr '\n' ' ')"
say "restart ok: ready in $((SECONDS-T0))s"
RESTARTS="$(podman inspect sensen-gw-pg --format '{{.RestartCount}}')"
say "restart count: $RESTARTS"

bash "$OFC/deploy/gateway/regression.sh" "$BASE" "$GK" > "$WORK/regress2.txt" 2>&1; rc=$?
say "regression after restart rc=$rc $(grep -oE '[0-9]+ passed, [0-9]+ failed' "$WORK/regress2.txt" | tail -1)"
[ $rc = 0 ] || die "regression failed after restart"

say "containers: $(podman ps -a --format '{{.Names}}:{{.Status}}' | grep sensen-gw | tr '\n' ' ')"
say "REDEPLOY COMPLETE sensen=$SC tbqwf=$TC"
