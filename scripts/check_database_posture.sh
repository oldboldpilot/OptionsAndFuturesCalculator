#!/usr/bin/env bash
# Assert the live security posture of both product databases.
#
# @author Olumuyiwa Oluwasanmi
#
# Neither database's posture is visible to behaviour: dropping an RLS policy,
# or restoring to a cluster that lost service_role's BYPASSRLS, changes no
# answer either application returns. Both have happened. This asserts the
# catalog directly and exits non-zero on any red check, so it can gate a deploy.
#
#   OFC_DATABASE_URL / DATABASE_URL  optionsandfuturescalculator.com
#   MFC_DATABASE_URL                 mortgagefvcalculator.com
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SQL="$REPO/backend/tests/test_database_posture.sql"
IMG=docker.io/library/postgres:18-alpine
rc=0
run_one() {
    local name="$1" url="$2"
    if [[ -z "$url" ]]; then
        echo "SKIP  $name -- no connection string set"; return 0
    fi
    echo "=== $name ==="
    if podman run --rm -i --network=host -e U="$url" -e PGCONNECT_TIMEOUT=20 \
         -v "$SQL:/tmp/p.sql:ro,Z" "$IMG" \
         sh -c 'psql "$U" -v ON_ERROR_STOP=1 -f /tmp/p.sql' 2>&1 \
       | sed -E 's#//[^@]*@#//<redacted>@#g' | grep -E 'PASS|FAIL|passed|[0-9]+ \|'; then
        echo "  $name: OK"
    else
        echo "  $name: FAILED"; rc=1
    fi
}
run_one optionsandfuturescalculator "${OFC_DATABASE_URL:-${DATABASE_URL:-}}"
run_one mortgagefvcalculator        "${MFC_DATABASE_URL:-}"
exit $rc
