# Shared helpers for the database migration tooling. Sourced, never executed.
#
# @author Olumuyiwa Oluwasanmi
#
# A TARGET names one database and is one of
#   url                      the hosted database, connection string in
#                            OFC_DATABASE_URL (or DATABASE_URL); psql runs in a
#                            throwaway container so the host needs no client
#   scratch:<container>:<db> a scratch container started by scratch_start
#
# The connection string reaches the container through the environment by NAME
# (-e DB_URL), never as an expanded argv word, so it is not visible in `ps`.
# Every error line passes through the repository's redaction expression.

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DB_DIR="$REPO/backend/db"
MIG_DIR="${MIG_DIR_OVERRIDE:-$DB_DIR/migrations}"
PG_IMAGE="${PG_IMAGE:-docker.io/library/postgres:18-alpine}"

redact() { sed -E 's#(postgres(ql)?://)[^@ ]*@#\1<redacted>@#g'; }

# pg_exec TARGET [psql args...]   stdin is passed through to psql.
pg_exec() {
    local target="$1"; shift
    case "$target" in
        scratch:*)
            local rest="${target#scratch:}" c db
            c="${rest%%:*}"; db="${rest#*:}"
            podman exec -i "$c" psql -X -q -U postgres -d "$db" "$@" 2> >(redact >&2)
            ;;
        url)
            DB_URL="${OFC_DATABASE_URL:-${DATABASE_URL:-}}"
            if [[ -z "$DB_URL" ]]; then
                echo "FATAL: OFC_DATABASE_URL (or DATABASE_URL) is not set" >&2; return 2
            fi
            export DB_URL
            podman run --rm -i --network=host -e PGCONNECT_TIMEOUT=20 -e DB_URL "$PG_IMAGE" \
                sh -c 'exec psql -X -q "$DB_URL" "$@"' sh "$@" 2> >(redact >&2)
            ;;
        *) echo "FATAL: unknown target '$target'" >&2; return 2 ;;
    esac
}

# Scalar query helper: one value, no alignment.
pg_scalar() { local t="$1"; shift; pg_exec "$t" -At -v ON_ERROR_STOP=1 -c "$1"; }

# scratch_start: start an empty throwaway server, print its container name.
# Unpublished port and trust auth on the container's own socket: reachable only
# through `podman exec`, so there is no password to leak.
scratch_start() {
    local name="ofc-scratch-$$-$RANDOM" i
    podman run -d --rm --name "$name" -e POSTGRES_HOST_AUTH_METHOD=trust \
        "${SCRATCH_IMAGE:-$PG_IMAGE}" >/dev/null || return 1
    for i in $(seq 1 60); do
        # the entrypoint starts a temporary server, stops it, then starts the
        # real one: ready means the "ready" line has been logged twice
        if [[ "$(podman logs "$name" 2>&1 | grep -c 'database system is ready to accept connections')" -ge 2 ]] \
           && podman exec "$name" psql -X -U postgres -Atc 'select 1' >/dev/null 2>&1; then
            echo "$name"; return 0
        fi
        sleep 1
    done
    echo "FATAL: scratch server $name did not become ready" >&2
    podman stop -t 5 "$name" >/dev/null 2>&1
    return 1
}
scratch_stop() { podman stop -t 5 "$1" >/dev/null 2>&1 || true; }

# scratch_db_create CONTAINER DB
scratch_db_create() { podman exec "$1" psql -X -q -U postgres -v ON_ERROR_STOP=1 -c "create database \"$2\"" >/dev/null; }

# Ordered list of migration files, one path per line.
mig_files() { ls -1 "$MIG_DIR"/[0-9][0-9][0-9][0-9]_*.sql 2>/dev/null | LC_ALL=C sort; }
mig_version() { local b; b="$(basename "$1")"; echo "${b%%_*}"; }
mig_name() { local b; b="$(basename "$1" .sql)"; echo "${b#*_}"; }
mig_sum() { sha256sum "$1" | cut -d' ' -f1; }

# pg_dump_exec TARGET [pg_dump args...]
pg_dump_exec() {
    local target="$1"; shift
    case "$target" in
        scratch:*)
            local rest="${target#scratch:}"
            podman exec -i "${rest%%:*}" pg_dump -U postgres -d "${rest#*:}" "$@" 2> >(redact >&2) ;;
        url)
            DB_URL="${OFC_DATABASE_URL:-${DATABASE_URL:-}}"
            [[ -z "$DB_URL" ]] && { echo "FATAL: OFC_DATABASE_URL (or DATABASE_URL) is not set" >&2; return 2; }
            export DB_URL
            podman run --rm -i --network=host -e PGCONNECT_TIMEOUT=20 -e DB_URL "$PG_IMAGE" \
                sh -c 'exec pg_dump "$DB_URL" "$@"' sh "$@" 2> >(redact >&2) ;;
    esac
}
