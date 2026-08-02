#!/usr/bin/env bash
#
# Pro-gate verification matrix.
#
# `smoke_client ... pro` proves the gate holds in both directions for the
# credentials it is handed. This script is what MAKES those credentials, and it
# makes the wrong ones too -- because "anonymous is refused, a subscriber is
# admitted" is satisfied just as well by a gate that reads the tier straight out
# of an unverified payload. The rows below are the ones that tell those two
# apart: a token whose tier claim was edited after signing, one signed with a
# different key, one that expired, one asking to be accepted with no signature
# at all, and the self-serve `user_metadata.tier` that a browser CAN write.
#
# Licences are minted by importing workers/billing/src/licence.ts DIRECTLY --
# the Worker's own code, not a reimplementation. That is deliberate: the whole
# risk in a mint-here/verify-there scheme is the two sides drifting apart, and a
# test that reimplements the minting side would agree with itself while
# production disagreed.
#
#   LICENCE_SIGNING_KEY=... SUPABASE_JWT_SECRET=... scripts/probe_pro_gate.sh [host:port]
#
# Both secrets must be the SAME values the engine under test is running with,
# and the engine must have PRO_GATE_MODE=enforce. Exits non-zero if any row
# disagrees, so it works as a release gate.

set -uo pipefail

TARGET="${1:-localhost:50051}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SMOKE="${SMOKE_CLIENT:-$REPO/backend/build/smoke_client}"
LICENCE_TS="$REPO/workers/billing/src/licence.ts"

die() { echo "probe_pro_gate: $*" >&2; exit 2; }

[ -x "$SMOKE" ] || die "smoke_client not found at $SMOKE (build it, or set SMOKE_CLIENT)"
[ -f "$LICENCE_TS" ] || die "missing $LICENCE_TS"
[ -n "${LICENCE_SIGNING_KEY:-}" ] || die "LICENCE_SIGNING_KEY is unset -- it must match the engine's"
[ -n "${SUPABASE_JWT_SECRET:-}" ] || die "SUPABASE_JWT_SECRET is unset -- it must match the engine's"
command -v node >/dev/null || die "node is required to mint test credentials"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---------------------------------------------------------------------------
# Mint every credential, valid and invalid.
# ---------------------------------------------------------------------------
cat > "$WORK/mint.mjs" <<MINT
import { mintLicence } from '$LICENCE_TS';
import fs from 'node:fs';
import crypto from 'node:crypto';

const dir = process.argv[2];
const lkey = process.env.LICENCE_SIGNING_KEY;
const jkey = process.env.SUPABASE_JWT_SECRET;
const now = Math.floor(Date.now() / 1000);
const b64 = (s) =>
  Buffer.from(s).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+\$/, '');
const w = (n, v) => fs.writeFileSync(\`\${dir}/\${n}\`, v);

// --- Licences, all through the Worker's real mintLicence. ---
w('lic.valid', await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now + 30 * 86400 }));

// Well past mintLicence's GRACE_DAYS, or this row would still be live and
// would prove nothing about expiry.
w('lic.expired', await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now - 10 * 86400 }));

// Sign a FREE licence, then swap the payload for a Pro one and keep the
// original signature. This is the exact attack the signature exists to stop.
const freeTok = await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'free', periodEndEpoch: now + 30 * 86400 });
const sig = freeTok.slice(freeTok.lastIndexOf('.') + 1);
w('lic.tampered',
  \`lk_live_\${b64(JSON.stringify({ s: 'cus_probe', t: 'pro', e: now + 33 * 86400, v: 1 }))}.\${sig}\`);

// Someone who worked out the FORMAT but not the secret.
w('lic.wrongkey', await mintLicence(crypto.randomBytes(48).toString('base64'), {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now + 30 * 86400 }));

// --- Supabase-shaped HS256 access tokens, as GoTrue issues them. ---
const jwt = (claims, { alg = 'HS256', swap = null } = {}) => {
  const h = b64(JSON.stringify({ alg, typ: 'JWT' }));
  let p = b64(JSON.stringify(claims));
  if (alg === 'none') return \`\${h}.\${p}.\`;
  const s = b64(crypto.createHmac('sha256', jkey).update(\`\${h}.\${p}\`).digest());
  if (swap) p = b64(JSON.stringify({ ...claims, ...swap }));
  return \`\${h}.\${p}.\${s}\`;
};
const base = (extra) => ({
  sub: '11111111-2222-3333-4444-555555555555', aud: 'authenticated',
  role: 'authenticated', email: 'probe@example.com',
  iat: now, exp: now + 3600, app_metadata: { provider: 'email' }, user_metadata: {}, ...extra,
});

w('jwt.pro',  jwt(base({ app_metadata: { provider: 'email', tier: 'pro' } })));
w('jwt.free', jwt(base({ app_metadata: { provider: 'email', tier: 'free' } })));

// user_metadata IS writable from the browser with the anon key, so a verifier
// that read the tier from there would be handing out free upgrades.
w('jwt.usermeta', jwt(base({
  app_metadata: { provider: 'email', tier: 'free' }, user_metadata: { tier: 'pro' } })));

// Signed as free, payload swapped to pro afterwards.
w('jwt.tampered', jwt(base({ app_metadata: { provider: 'email', tier: 'free' } }),
  { swap: { app_metadata: { provider: 'email', tier: 'pro' } } }));

w('jwt.expired', jwt(base({ exp: now - 60, app_metadata: { provider: 'email', tier: 'pro' } })));

// alg:none asks to be accepted with no signature at all -- the classic JWT break.
w('jwt.algnone', jwt(base({ app_metadata: { provider: 'email', tier: 'pro' } }), { alg: 'none' }));

w('jwt.garbage', 'not-a-jwt-at-all');
w('lic.garbage', 'lk_live_bm90cmVhbA.bm90cmVhbA');
MINT

node "$WORK/mint.mjs" "$WORK" || die "could not mint test credentials"

# ---------------------------------------------------------------------------
# Assert each one. smoke_client exits 0 only when the 4-leg call was ALLOWED
# for the credential it was given, so a `deny` row passes when it exits non-zero.
# ---------------------------------------------------------------------------
pass=0; fail=0

row() {
  local name="$1" expect="$2" kind="$3" file="$4"
  local out rc actual
  if [ "$kind" = jwt ]; then
    out=$(PRO_GATE_MODE=enforce SMOKE_PRO_BEARER="$(cat "$WORK/$file")" \
          "$SMOKE" "$TARGET" SPY pro 2>&1); rc=$?
  else
    out=$(PRO_GATE_MODE=enforce SMOKE_PRO_LICENCE="$(cat "$WORK/$file")" \
          "$SMOKE" "$TARGET" SPY pro 2>&1); rc=$?
  fi
  actual=deny; [ $rc -eq 0 ] && actual=allow

  if [ "$actual" = "$expect" ]; then
    printf '  PASS  %-44s expected %-5s got %s\n' "$name" "$expect" "$actual"
    pass=$((pass + 1))
  else
    printf '  FAIL  %-44s expected %-5s got %s\n' "$name" "$expect" "$actual"
    printf '%s\n' "$out" | sed 's/^/          | /'
    fail=$((fail + 1))
  fi
}

echo "Pro-gate matrix against $TARGET (4-leg CalculateStrategy, PRO_GATE_MODE=enforce)"
echo

row "signed-in, app_metadata.tier=pro"          allow jwt jwt.pro
row "signed-in, app_metadata.tier=free"         deny  jwt jwt.free
row "user_metadata.tier=pro (browser-writable)" deny  jwt jwt.usermeta
row "JWT payload edited after signing"          deny  jwt jwt.tampered
row "expired JWT, tier=pro"                     deny  jwt jwt.expired
row "alg:none, tier=pro, no signature"          deny  jwt jwt.algnone
row "garbage bearer token"                      deny  jwt jwt.garbage
row "valid Pro licence"                         allow lic lic.valid
row "expired licence"                           deny  lic lic.expired
row "licence payload edited after signing"      deny  lic lic.tampered
row "licence signed with the wrong key"         deny  lic lic.wrongkey
row "garbage licence"                           deny  lic lic.garbage

echo
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
