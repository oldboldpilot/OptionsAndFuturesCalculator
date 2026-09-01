#!/usr/bin/env python3
"""
Gates for the Apple client-secret minting logic.

@author Olumuyiwa Oluwasanmi

NO EXTERNAL TEST FRAMEWORK, matching this repository's rule 39 and the C++
suites beside it: a `check()` and two counters, non-zero exit on failure.

WHAT THIS CAN AND CANNOT COVER. It signs with a THROWAWAY P-256 key generated
per run, so every claim, the signature and the lifetime ceiling are exercised
for real. What it cannot do is ask Apple whether the token is acceptable --
that needs a genuine Team ID, Service ID and .p8, and a network call to
appleid.apple.com. The claims asserted below are exactly the ones Apple
validates, so a token that passes here and is still refused means the IDs are
wrong, not the minting.
"""
import sys
import time

sys.path.insert(0, "scripts")

import jwt  # noqa: E402
from cryptography.hazmat.primitives import serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402

import apple_client_secret as acs  # noqa: E402

CHECKS = 0
FAILURES = 0


def check(condition: bool, what: str) -> None:
    global CHECKS, FAILURES
    CHECKS += 1
    print(f"  {'PASS' if condition else 'FAIL'}: {what}")
    if not condition:
        FAILURES += 1


def section(title: str) -> None:
    print(f"\n=== {title} ===")


def throwaway_identity(service_id: str = "com.example.web") -> acs.AppleIdentity:
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ).decode()
    return acs.AppleIdentity(
        team_id="ABCDE12345", key_id="XYZ9876543", service_id=service_id, private_key_pem=pem
    ), key


def main() -> int:
    print("Apple client secret gates")

    section("1. A minted secret carries the claims Apple checks")
    identity, key = throwaway_identity()
    token = acs.mint(identity, 180)
    claims = acs.decode_unverified(token)
    check(claims["iss"] == identity.team_id, "iss is the TEAM id")
    # These two are the swap that produces a structurally perfect token Apple
    # refuses as invalid_client -- indistinguishable from expiry at the call
    # site, which is why both directions are pinned rather than just one.
    check(claims["sub"] == identity.service_id, "sub is the SERVICE id, not the team id")
    check(claims["sub"] != claims["iss"], "sub and iss are not the same value")
    check(claims["aud"] == "https://appleid.apple.com", "aud is Apple's token endpoint")

    section("2. The signature verifies, and it is ES256")
    header = jwt.get_unverified_header(token)
    check(header["alg"] == "ES256", "alg is ES256 -- Apple rejects RS256 here")
    check(header["kid"] == identity.key_id, "kid names the .p8's Key ID")
    pub = key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    verified = jwt.decode(token, pub, algorithms=["ES256"], audience="https://appleid.apple.com")
    check(verified["sub"] == identity.service_id, "the signature verifies against the key")

    section("3. Apple's six-month ceiling is refused, not silently clamped")
    # Clamping would produce a token shorter than requested with nothing saying
    # so -- the same objection this repo makes to clamping out-of-range Census
    # values rather than refusing them.
    try:
        acs.mint(identity, 200)
        check(False, "200 days should have been refused")
    except SystemExit as exc:
        check("caps the lifetime" in str(exc), "200 days is REFUSED with a message naming the cap")
    check(acs.mint(identity, 182) is not None, "182 days, just inside the cap, is accepted")

    section("4. days_remaining reads an expiry we did not sign")
    # The rotate path must read `exp` off the DEPLOYED secret, for which we
    # hold no key. Signature verification is deliberately not attempted.
    now = int(time.time())
    old = acs.mint(identity, 180, now=now - 175 * 86_400)
    left = acs.days_remaining(old)
    check(4.0 < left < 6.0, f"a 175-day-old 180-day token reports ~5 days left (got {left:.1f})")
    fresh = acs.mint(identity, 180, now=now)
    check(acs.days_remaining(fresh) > 179, "a fresh token reports its full life")

    section("5. The GoTrue variable name is pinned")
    # Script and runbook must not drift; GoTrue reads this exact name.
    check(
        acs.GOTRUE_SECRET_VAR == "GOTRUE_EXTERNAL_APPLE_SECRET",
        "the variable GoTrue reads is GOTRUE_EXTERNAL_APPLE_SECRET",
    )

    print(f"\n{CHECKS} checks, {FAILURES} failures")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
