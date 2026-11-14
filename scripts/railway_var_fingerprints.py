#!/usr/bin/env python3
"""Fingerprint a Railway service's variables WITHOUT printing any value.

@author Olumuyiwa Oluwasanmi

WHY A FINGERPRINT AND NOT A LISTING.

During a credential rotation the question asked over and over is "do these two
stores hold the same string?" -- does Railway's STRIPE_LIVE_API_KEY match the
one in config/.env, does GOTRUE_JWT_SECRET still equal SUPABASE_JWT_SECRET, did
this service get the new value or the old one. `railway variables` answers it by
printing every secret the service holds into the terminal, the scrollback and
whatever is capturing them. That is a leak performed in order to check for a
leak.

A SHA-256 prefix answers the same question and reveals nothing: equal
fingerprints mean equal values, different fingerprints mean different values,
and twelve hex characters of a digest cannot be walked back to a 107-character
key. The length is printed beside it because a rotation that produces a value of
a different SHAPE (an `rk_` restricted key where an `sk_` secret key belongs) is
worth seeing before it reaches production, and a length is not a secret.

The Stripe key PREFIX is printed too -- `sk_live_`, `rk_live_`, `pk_test_` --
because it is the part of a Stripe key that is already public by design (it is
visible in the dashboard beside the masked remainder) and it is exactly what
distinguishes a usable key from one that will fail with a permissions error
rather than an authentication one.

USAGE
    scripts/railway_var_fingerprints.py                      # linked service
    scripts/railway_var_fingerprints.py --service mfv-web
    scripts/railway_var_fingerprints.py --match 'STRIPE|ALPACA'

EXIT CODES: 0 listed, 1 the railway CLI failed (its stderr is relayed), 2 the
output was not the JSON object this expects.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys

# A Stripe key's prefix is public by design; the remainder never leaves here.
STRIPE_PREFIX = re.compile(r"^[a-z]{2,3}_(live|test)_")


def fingerprint(value: str) -> str:
    """Twelve hex characters of SHA-256: enough to compare, not to reconstruct."""
    return hashlib.sha256(value.encode()).hexdigest()[:12]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--service", help="service name; default is the linked one")
    ap.add_argument("--environment", help="environment name; default is the linked one")
    ap.add_argument("--match", help="only names matching this regular expression")
    args = ap.parse_args()

    cmd = ["railway", "variables", "--json"]
    if args.service:
        cmd += ["--service", args.service]
    if args.environment:
        cmd += ["--environment", args.environment]

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        # The CLI's own diagnostics are relayed; its stdout is NOT, because on
        # some failures it has already emitted part of the variable listing.
        sys.stderr.write(proc.stderr)
        return 1
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        sys.stderr.write("railway did not return JSON; refusing to print its output\n")
        return 2
    if not isinstance(data, dict):
        sys.stderr.write(f"expected a JSON object of name -> value, got {type(data).__name__}\n")
        return 2

    pattern = re.compile(args.match, re.IGNORECASE) if args.match else None
    names = sorted(n for n in data if pattern is None or pattern.search(n))
    if not names:
        print("(no variable names matched)")
        return 0

    print(f"{'NAME':<34} {'CHARS':>6}  {'SHAPE':<26} FINGERPRINT")
    for name in names:
        value = data[name]
        if not isinstance(value, str):
            value = "" if value is None else json.dumps(value)
        m = STRIPE_PREFIX.match(value)
        shape = m.group(0) if m else ("(empty)" if not value else "")
        print(f"{name:<34} {len(value):>6}  {shape:<26} sha256:{fingerprint(value)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
