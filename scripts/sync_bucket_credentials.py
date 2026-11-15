#!/usr/bin/env python3
"""Make each service's BUCKET_* variables equal the bucket's LIVE S3 credentials.

@author Olumuyiwa Oluwasanmi

WHY THIS EXISTS.

`railway bucket credentials --reset` mints a new secret for the bucket. Nothing
updates the services that hold a COPY of it, and nothing warns that they now
hold a dead one: the name is right, the length is right, only the value is
stale. The build then fails at the model fetch with a bare

    curl: (22) The requested URL returned error: 403
    FATAL: SigV4 fetch of the strategy encoder failed.

which reads as "the bucket is gone" or "the object moved". Measured 2026-11-14:
both options-calculator-backend and mfv-assistant carried the correct
accessKeyId and a secretAccessKey from before a reset, and both deployments
failed at the same step for that one reason.

Railway has no variable-reference syntax for bucket credentials -- `${{...}}`
reaches services and shared variables, not a bucket -- so the copy cannot be
replaced by a reference. What it CAN be is re-derived on demand, which is what
this does.

WHAT IT PRINTS, AND WHAT IT DOES NOT. Names, lengths and twelve hex characters
of SHA-256. Equal fingerprints mean equal values; twelve hex characters cannot
be walked back to a 75-character secret. No value is ever printed.

THE ONE EXPOSURE, STATED PLAINLY. `railway variable set` takes NAME=VALUE as a
command-line argument -- the Railway CLI offers no stdin path -- so during that
call the value is visible in this host's process list. It is never written to a
shell history by this script (the argv is built in Python, not by a shell) and
never echoed. `--check` performs no write at all, and is the default.

USAGE
    scripts/sync_bucket_credentials.py                      # compare only
    scripts/sync_bucket_credentials.py --write              # fix what differs
    scripts/sync_bucket_credentials.py --bucket model-artifacts \\
        --service options-calculator-backend --service mfv-assistant

EXIT: 0 everything already matches (or was written), 1 a mismatch remains or a
railway call failed, 2 usage/parse error.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys

# bucket credential field -> the service variable that must carry it
MAPPING = {
    "accessKeyId": "BUCKET_ACCESS_KEY_ID",
    "secretAccessKey": "BUCKET_SECRET_ACCESS_KEY",
}


def fingerprint(value: str) -> str:
    """Twelve hex characters of SHA-256: enough to compare, not to reconstruct."""
    return hashlib.sha256(value.encode()).hexdigest()[:12]


def run(cmd: list[str]) -> tuple[int, str, str]:
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def live_credentials(bucket: str, environment: str | None) -> dict[str, str]:
    cmd = ["railway", "bucket", "credentials", "--bucket", bucket, "--json"]
    if environment:
        cmd += ["--environment", environment]
    rc, out, err = run(cmd)
    if rc != 0:
        sys.stderr.write(err or "railway bucket credentials failed\n")
        raise SystemExit(1)
    try:
        return json.loads(out)
    except json.JSONDecodeError:
        sys.stderr.write("railway did not return JSON; refusing to print its output\n")
        raise SystemExit(2)


def service_variables(service: str, environment: str | None) -> dict[str, str]:
    cmd = ["railway", "variables", "--service", service, "--json"]
    if environment:
        cmd += ["--environment", environment]
    rc, out, err = run(cmd)
    if rc != 0:
        sys.stderr.write(err or f"railway variables failed for {service}\n")
        raise SystemExit(1)
    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        sys.stderr.write("railway did not return JSON; refusing to print its output\n")
        raise SystemExit(2)
    if not isinstance(data, dict):
        sys.stderr.write(f"expected an object of name -> value, got {type(data).__name__}\n")
        raise SystemExit(2)
    return data


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bucket", default="model-artifacts")
    ap.add_argument("--environment")
    ap.add_argument(
        "--service",
        action="append",
        default=None,
        help="repeatable; defaults to the two services that fetch a model at build time",
    )
    ap.add_argument("--write", action="store_true", help="actually set what differs")
    args = ap.parse_args()

    services = args.service or ["options-calculator-backend", "mfv-assistant"]

    creds = live_credentials(args.bucket, args.environment)
    missing = [f for f in MAPPING if not creds.get(f)]
    if missing:
        sys.stderr.write(f"bucket credentials lack {', '.join(missing)}\n")
        return 1

    print(f"bucket {args.bucket} ({creds.get('endpoint')}), live credentials:")
    for field, var in MAPPING.items():
        v = creds[field]
        print(f"  {field:<18} -> {var:<26} {len(v):>3} chars  sha256:{fingerprint(v)}")
    print()

    drift = 0
    for service in services:
        print(f"{service}:")
        have = service_variables(service, args.environment)
        for field, var in MAPPING.items():
            want = creds[field]
            got = have.get(var)
            if got is None:
                state, differs = "ABSENT", True
            elif got == want:
                state, differs = "matches", False
            else:
                state, differs = "STALE", True
            got_fp = f"sha256:{fingerprint(got)}" if isinstance(got, str) else "(unset)"
            print(f"  {var:<26} {state:<8} service={got_fp}  live=sha256:{fingerprint(want)}")
            if not differs:
                continue
            drift += 1
            if not args.write:
                continue
            cmd = ["railway", "variable", "set", f"{var}={want}", "--service", service]
            if args.environment:
                cmd += ["--environment", args.environment]
            rc, _out, err = run(cmd)
            # The value is in this child's argv by necessity; it is not echoed here.
            if rc != 0:
                sys.stderr.write(err or f"failed to set {var} on {service}\n")
                return 1
            print(f"  {var:<26} WRITTEN  -> sha256:{fingerprint(want)}")
            drift -= 1
        print()

    if drift and not args.write:
        print(f"{drift} variable(s) differ. Re-run with --write to fix them.")
        return 1
    print("every BUCKET_* variable matches the bucket's live credentials.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
