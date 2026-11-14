#!/usr/bin/env python3
"""Fingerprint the Stripe CLI's stored session credential WITHOUT printing it.

@author Olumuyiwa Oluwasanmi

WHY THIS EXISTS.

`stripe login` does not merely authenticate a terminal: it creates a RESTRICTED
API KEY (`rk_live_...`) on the account and stores it in the CLI's config. That
key EXPIRES -- Stripe gives a CLI session key a 90-day life -- and when it does,
every caller still holding it gets

    Expired API Key provided: rk_live_****...

which is the exact error this project's billing Worker began returning. So the
question "is the key the Worker holds the same one the CLI holds, and has the
CLI's been refreshed since?" decides whether a rotation is needed at all or
whether a `stripe login` has already produced the replacement.

Answering it by printing the key would defeat the point. A SHA-256 prefix
answers it exactly: equal fingerprints mean the two stores hold the same string.
The key's PREFIX (`rk_live_`, `sk_live_`) is printed because it is public by
design and because it is what distinguishes a CLI session key -- which expires,
and must therefore never be a production credential -- from a dashboard-created
key, which does not.

USAGE
    scripts/stripe_session_witness.py
    scripts/stripe_session_witness.py --compare <sha256-prefix>

EXIT CODES: 0 fingerprinted (and, with --compare, equal), 3 fingerprinted and
DIFFERENT from --compare, 1 no CLI config, 2 no credential found in it.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "stripe" / "config.toml"
# A Stripe key's prefix is public by design; the remainder never leaves here.
KEY_PREFIX = re.compile(r"^[a-z]{2,3}_(live|test)_")


def witness(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()[:12]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compare", help="a fingerprint to compare against")
    args = ap.parse_args()

    found: list[tuple[str, str, str]] = []

    # Newer CLI releases keep the session credential in credentials.json rather
    # than config.toml, so BOTH are searched: finding neither is a different
    # answer from finding one that is empty, and reporting "no credential" when
    # the file simply moved would be a false negative of exactly the kind this
    # script exists to avoid.
    creds = CONFIG.parent / "credentials.json"
    if creds.is_file():
        import json

        def walk(node, trail: str) -> None:
            if isinstance(node, dict):
                for k, v in node.items():
                    walk(v, f"{trail}.{k}" if trail else str(k))
            elif isinstance(node, list):
                for i, v in enumerate(node):
                    walk(v, f"{trail}[{i}]")
            elif isinstance(node, str) and KEY_PREFIX.match(node):
                head, _, tail = trail.rpartition(".")
                found.append((f"credentials.json:{head or '(root)'}", tail or trail, node))

        try:
            walk(json.loads(creds.read_text(errors="ignore")), "")
        except json.JSONDecodeError:
            sys.stderr.write("credentials.json is not valid JSON; skipped\n")

    if not CONFIG.is_file() and not found:
        sys.stderr.write(f"no Stripe CLI config at {CONFIG}; run `stripe login` first\n")
        return 1

    section = "config.toml"
    for raw in (CONFIG.read_text(errors="ignore").splitlines() if CONFIG.is_file() else []):
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if "=" not in line or line.startswith("#"):
            continue
        name, _, value = line.partition("=")
        name = name.strip()
        value = value.strip().strip('"').strip("'")
        if not KEY_PREFIX.match(value):
            continue
        found.append((section, name, value))

    if not found:
        sys.stderr.write("no Stripe-shaped credential in the CLI config\n")
        return 2

    rc = 0
    print(f"{'PROFILE':<22} {'FIELD':<24} {'CHARS':>6}  {'PREFIX':<12} FINGERPRINT")
    for section, name, value in found:
        prefix = KEY_PREFIX.match(value).group(0)
        fp = witness(value)
        mark = ""
        if args.compare:
            same = fp.startswith(args.compare) or args.compare.startswith(fp)
            mark = "  <= SAME as --compare" if same else "  <= DIFFERENT from --compare"
            if not same:
                rc = 3
        print(f"{section:<22} {name:<24} {len(value):>6}  {prefix:<12} sha256:{fp}{mark}")

    if any(p.startswith("rk_") for _, _, p in ((s, n, v[:3]) for s, n, v in found)):
        print()
        print("NOTE: an `rk_` key here is a CLI SESSION key created by `stripe login`.")
        print("      Stripe expires those after 90 days. It is fine for this terminal and")
        print("      WRONG for a deployed service, which would simply stop taking payments")
        print("      on the ninetieth day with `Expired API Key provided`.")
    return rc


if __name__ == "__main__":
    sys.exit(main())
