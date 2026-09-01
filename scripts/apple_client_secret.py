#!/usr/bin/env python3
"""
Mint (and rotate) the Apple "Sign in with Apple" client secret.

@author Olumuyiwa Oluwasanmi

WHY THIS EXISTS AND WHY IT IS AUTOMATED FROM DAY ONE

Google's OAuth client secret is a static string that never expires. Apple's is
not a secret at all -- it is an ES256 JWT you mint yourself from a .p8 signing
key, and Apple caps its lifetime at SIX MONTHS (15,777,000 seconds). When it
lapses, every Apple sign-in fails at once, with no prior warning and no
degraded mode: the provider simply starts returning invalid_client.

That failure has three properties that make it worth automating BEFORE the
first one is ever issued rather than after the first outage:

  * It is silent until it is total. Nothing warns at five months.
  * It arrives on a date nobody remembers, roughly two quarters after the
    person who set it up has moved on to something else.
  * The fix requires a .p8 file that lives in a password manager, so the
    person on call at 2am is unlikely to be able to do it.

A calendar reminder is the usual answer and it is the wrong one, for the same
reason this repository schedules the Census refresh on "has a run succeeded in
the last eight days?" rather than on a weekly calendar slot: a reminder can be
missed, snoozed or inherited by somebody who does not know what it means. A
job that asks about the OUTCOME cannot miss.

WHAT IT DOES

  mint     -- print a fresh client secret (never logs it; writes to stdout only
              when explicitly asked, so a CI log does not capture it)
  inspect  -- decode the CURRENT secret and report days remaining, no key needed
  rotate   -- mint and push to the Railway GoTrue service, but ONLY if the
              current one expires within --renew-before days. Idempotent, so it
              is safe to run daily.

USAGE

  export APPLE_TEAM_ID=ABCDE12345          # Apple Developer -> Membership
  export APPLE_KEY_ID=XYZ9876543           # the .p8's Key ID
  export APPLE_SERVICE_ID=com.example.web  # the SERVICE ID, not the App ID
  export APPLE_PRIVATE_KEY_P8="$(cat AuthKey_XYZ9876543.p8)"

  python3 scripts/apple_client_secret.py inspect
  python3 scripts/apple_client_secret.py rotate --service supabase-auth
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass

try:
    import jwt  # PyJWT
    from cryptography.hazmat.primitives import serialization
except ImportError:  # pragma: no cover - environment problem, not a logic path
    print("needs PyJWT and cryptography: pip install pyjwt cryptography", file=sys.stderr)
    raise SystemExit(2)

# Apple's hard ceiling. Requesting more is rejected outright, so this is a
# bound rather than a preference. 180 days sits just inside it and keeps the
# arithmetic legible.
MAX_LIFETIME_SECONDS = 15_777_000
DEFAULT_LIFETIME_DAYS = 180
APPLE_AUDIENCE = "https://appleid.apple.com"

# The variable GoTrue reads. Named here once so the script and the runbook
# cannot drift apart.
GOTRUE_SECRET_VAR = "GOTRUE_EXTERNAL_APPLE_SECRET"


@dataclass(frozen=True)
class AppleIdentity:
    team_id: str
    key_id: str
    service_id: str
    private_key_pem: str

    @staticmethod
    def from_env() -> "AppleIdentity":
        missing = [
            name
            for name in (
                "APPLE_TEAM_ID",
                "APPLE_KEY_ID",
                "APPLE_SERVICE_ID",
                "APPLE_PRIVATE_KEY_P8",
            )
            if not os.environ.get(name)
        ]
        if missing:
            raise SystemExit(f"missing environment: {', '.join(missing)}")
        return AppleIdentity(
            team_id=os.environ["APPLE_TEAM_ID"].strip(),
            key_id=os.environ["APPLE_KEY_ID"].strip(),
            service_id=os.environ["APPLE_SERVICE_ID"].strip(),
            private_key_pem=os.environ["APPLE_PRIVATE_KEY_P8"],
        )


def mint(identity: AppleIdentity, lifetime_days: int, now: int | None = None) -> str:
    """Return a freshly signed client secret.

    `sub` is the SERVICE ID and `iss` is the TEAM ID. Getting those the wrong
    way round produces a JWT that is structurally valid, signs correctly, and
    is refused by Apple with `invalid_client` -- indistinguishable from an
    expired one, which is why they are named rather than positional here.
    """
    issued = int(time.time()) if now is None else now
    lifetime = lifetime_days * 86_400
    if lifetime > MAX_LIFETIME_SECONDS:
        raise SystemExit(
            f"Apple caps the lifetime at {MAX_LIFETIME_SECONDS}s "
            f"({MAX_LIFETIME_SECONDS // 86_400} days); {lifetime_days} was asked for"
        )

    key = serialization.load_pem_private_key(
        identity.private_key_pem.encode("utf-8"), password=None
    )
    return jwt.encode(
        {
            "iss": identity.team_id,
            "iat": issued,
            "exp": issued + lifetime,
            "aud": APPLE_AUDIENCE,
            "sub": identity.service_id,
        },
        key,
        algorithm="ES256",
        headers={"kid": identity.key_id, "alg": "ES256"},
    )


def decode_unverified(token: str) -> dict:
    """Read the claims WITHOUT verifying the signature.

    Deliberate: the point is to read `exp` off a secret we did not mint and do
    not hold the key for -- the running one, fetched from Railway. Verifying
    would need the key and would defeat the purpose, and nothing here trusts
    the contents for an access decision.
    """
    payload = token.split(".")[1]
    payload += "=" * (-len(payload) % 4)
    return json.loads(base64.urlsafe_b64decode(payload))


def railway_get(service: str, variable: str) -> str | None:
    out = subprocess.run(
        ["railway", "variables", "--service", service, "--kv"],
        capture_output=True,
        text=True,
        check=False,
    )
    if out.returncode != 0:
        raise SystemExit(f"railway variables failed: {out.stderr.strip()[:200]}")
    for line in out.stdout.splitlines():
        name, _, value = line.partition("=")
        if name.strip() == variable:
            return value.strip()
    return None


def railway_set(service: str, variable: str, value: str) -> None:
    # --skip-deploys because GoTrue reads this at boot; the caller decides when
    # to restart, so a rotation never causes an unplanned deploy mid-day.
    out = subprocess.run(
        ["railway", "variables", "--service", service, "--set", f"{variable}={value}",
         "--skip-deploys"],
        capture_output=True,
        text=True,
        check=False,
    )
    if out.returncode != 0:
        raise SystemExit(f"railway variables --set failed: {out.stderr.strip()[:200]}")


def days_remaining(token: str) -> float:
    return (decode_unverified(token).get("exp", 0) - time.time()) / 86_400


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_mint = sub.add_parser("mint", help="print a fresh client secret to stdout")
    p_mint.add_argument("--days", type=int, default=DEFAULT_LIFETIME_DAYS)

    p_inspect = sub.add_parser("inspect", help="report the deployed secret's remaining life")
    p_inspect.add_argument("--service", default="supabase-auth")

    p_rot = sub.add_parser("rotate", help="rotate if close to expiry; idempotent")
    p_rot.add_argument("--service", default="supabase-auth")
    p_rot.add_argument("--days", type=int, default=DEFAULT_LIFETIME_DAYS)
    p_rot.add_argument(
        "--renew-before",
        type=int,
        default=30,
        help="rotate when fewer than this many days remain (default 30)",
    )
    p_rot.add_argument("--force", action="store_true", help="rotate regardless of remaining life")

    args = ap.parse_args()

    if args.cmd == "mint":
        # The ONLY path that writes the secret anywhere. Everything else keeps
        # it inside the process.
        print(mint(AppleIdentity.from_env(), args.days))
        return 0

    if args.cmd == "inspect":
        current = railway_get(args.service, GOTRUE_SECRET_VAR)
        if not current:
            print(f"{GOTRUE_SECRET_VAR} is not set on '{args.service}' -- Apple login is not configured")
            return 1
        claims = decode_unverified(current)
        left = days_remaining(current)
        print(f"service id (sub): {claims.get('sub')}")
        print(f"team id    (iss): {claims.get('iss')}")
        print(f"expires        : {time.strftime('%Y-%m-%d', time.gmtime(claims.get('exp', 0)))}")
        print(f"days remaining : {left:.1f}")
        return 0 if left > 0 else 1

    # rotate
    current = railway_get(args.service, GOTRUE_SECRET_VAR)
    if current and not args.force:
        left = days_remaining(current)
        if left > args.renew_before:
            print(f"no action: {left:.1f} days remain (threshold {args.renew_before})")
            return 0
        print(f"rotating: only {left:.1f} days remain")
    elif not current:
        print("rotating: no secret currently set")

    identity = AppleIdentity.from_env()
    fresh = mint(identity, args.days)

    # Verify what we are about to deploy BEFORE deploying it. A malformed key
    # or a swapped team/service id yields a token that looks fine and is
    # refused by Apple; checking the claims here turns that into a failed
    # rotation instead of a silent outage in six months' time.
    claims = decode_unverified(fresh)
    assert claims["sub"] == identity.service_id, "sub must be the Service ID"
    assert claims["iss"] == identity.team_id, "iss must be the Team ID"
    assert claims["aud"] == APPLE_AUDIENCE
    assert claims["exp"] - claims["iat"] <= MAX_LIFETIME_SECONDS

    railway_set(args.service, GOTRUE_SECRET_VAR, fresh)
    print(
        f"rotated: expires {time.strftime('%Y-%m-%d', time.gmtime(claims['exp']))} "
        f"({args.days} days). Restart '{args.service}' for GoTrue to pick it up."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
