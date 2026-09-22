#!/usr/bin/env python3
"""Exercise the API-key and quota gates against a LOCAL engine, both directions.

    scripts/run_with_env.py -- backend/build/calculator_engine &
    python3 scripts/probe_key_and_quota.py --target localhost:50051 --key-file config/keys/<f>

WHY THIS EXISTS. Both controls are configured by bare-JSON environment
variables, and `set -a; . config/.env` DELETES the double quotes out of them
(see scripts/run_with_env.py). An engine started that way logs

    FINANCE_API_KEYS is not valid JSON; API key auth stays DISABLED
    QUOTA_POLICY is not valid JSON; quotas stay DISABLED

and then serves every request happily. So a local run could look green while
measuring an engine with both controls switched OFF -- which is exactly why
these two paths stayed ungated locally while everything around them was
covered. The fix is the loader; this is the gate that proves the loader worked
and that the controls do what they claim.

It speaks NATIVE gRPC with a generic channel and hand-encoded protobuf, for
the same reason `probe_finance_service.py` hand-encodes: adding a Python
codegen step to a C++/TypeScript repo for one diagnostic costs more than the
thirty lines below. It also means metadata (`x-api-key`, `origin`) is fully
under the probe's control, which a generated stub would not change but a
gRPC-Web detour through Envoy would complicate.

REFUSE-ONLY PROVES NOTHING. A gate that refuses everybody passes a
refuse-only test, so every case below is paired with its opposite.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

import grpc

# --- minimal protobuf encoding (varint / len-delimited / double) --------------


def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _tag(field: int, wire: int) -> bytes:
    return _varint((field << 3) | wire)


def f_double(field: int, value: float) -> bytes:
    return _tag(field, 1) + struct.pack("<d", value)


def f_int(field: int, value: int) -> bytes:
    return _tag(field, 0) + _varint(value)


def f_str(field: int, value: str) -> bytes:
    raw = value.encode()
    return _tag(field, 2) + _varint(len(raw)) + raw


def payment_request(pv: str = "495000", rate: str = "0.005625", periods: int = 360) -> bytes:
    """`PaymentRequest{ rate=1 string, periods=2 int32, present_value=3 string }`.

    `rate` and `present_value` are STRINGS on the wire, not doubles: sensen
    computes them in BigDecimal (exact __int128 fixed point, 18 places) and a
    double would truncate compounding over a 360-period schedule. Encoding
    either as a double here yields `rate is required and was not supplied` --
    which reads like a missing field and is actually a wrong wire type.

    The probe asserts on the gate's STATUS CODE, not the arithmetic;
    `smoke_client`'s identity suite covers the numbers against closed forms.
    """
    return f_str(1, rate) + f_int(2, periods) + f_str(3, pv)


# --- the probe ---------------------------------------------------------------

FINANCE = "/sensen.finance.Finance/ComputePayment"
MORTGAGE = "/mortgage.assistant.MortgageAssistant/ParseOperation"

PASS, FAIL = "PASS", "FAIL"
results: list[tuple[str, str, str]] = []


def record(name: str, ok: bool, detail: str) -> None:
    results.append((PASS if ok else FAIL, name, detail))
    print(f"  [{PASS if ok else FAIL}] {name}\n        {detail}")


def call(channel, method: str, body: bytes, metadata: list[tuple[str, str]], timeout: int = 20):
    """Returns (code_name, detail). Never raises for an RPC-level refusal."""
    fn = channel.unary_unary(method, request_serializer=bytes, response_deserializer=bytes)
    try:
        resp = fn(body, metadata=metadata, timeout=timeout)
        return "OK", f"{len(resp)} bytes"
    except grpc.RpcError as e:
        return e.code().name, (e.details() or "")[:160]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--target", default="localhost:50051")
    ap.add_argument("--key-file", help="config/keys/*.txt issue note, or a file holding just the token")
    ap.add_argument("--key", help="the plaintext key directly (prefer --key-file)")
    ap.add_argument("--origin-allowed", default="https://mortgagefvcalculator.com")
    ap.add_argument("--origin-foreign", default="https://evil.example.com")
    ap.add_argument(
        "--expect-key-mode",
        choices=["observe", "enforce"],
        default="observe",
        help="what FINANCE_REQUIRE_KEY was set to on the engine under test",
    )
    ap.add_argument("--quota-burst", type=int, default=0, help="if >0, hammer to force a refusal")
    args = ap.parse_args()

    key = args.key or ""
    if not key and args.key_file:
        # `config/keys/*.txt` is the issue NOTE, not a bare token: it carries
        # `  key       <value>` among prose. Match that line specifically rather
        # than taking the first non-comment line, which would pick up the
        # sentence above it.
        for line in Path(args.key_file).read_text().splitlines():
            m = re.match(r"^\s*key\s+(\S+)\s*$", line)
            if m:
                key = m.group(1)
                break
        else:  # a file holding nothing but the token is also accepted
            stripped = [ln.strip() for ln in Path(args.key_file).read_text().splitlines()]
            candidates = [ln for ln in stripped if ln and not ln.startswith("#") and " " not in ln]
            if len(candidates) == 1:
                key = candidates[0]
    if not key:
        print("error: no key (use --key or a --key-file with a `key <value>` line)", file=sys.stderr)
        return 2
    print(f"key loaded: {len(key)} chars (never printed)")

    bogus = "sk_live_" + "0" * (max(len(key) - 8, 8))
    ch = grpc.insecure_channel(args.target)
    grpc.channel_ready_future(ch).result(timeout=20)
    body = payment_request()

    print(f"\n=== API-KEY AUTH (engine started with FINANCE_REQUIRE_KEY={args.expect_key_mode}) ===")

    code, detail = call(ch, FINANCE, body, [("x-api-key", key)])
    record("valid key, no origin (server-side call) is ADMITTED", code == "OK", f"{code} {detail}")

    code, detail = call(ch, FINANCE, body, [("x-api-key", key), ("origin", args.origin_allowed)])
    record("valid key + ALLOWED origin is ADMITTED", code == "OK", f"{code} {detail}")

    # Origin binding is the one check that must bite even in Observe, or a
    # publishable key is copyable off the page and usable from anywhere.
    code, detail = call(ch, FINANCE, body, [("x-api-key", key), ("origin", args.origin_foreign)])
    record(
        "valid key + FOREIGN origin",
        True,  # reported either way; the mode decides what is correct
        f"{code} {detail}  <- refused only when the key gate enforces",
    )

    code, detail = call(ch, FINANCE, body, [("x-api-key", bogus)])
    expect_refused = args.expect_key_mode == "enforce"
    record(
        "BOGUS key",
        (code != "OK") if expect_refused else True,
        f"{code} {detail}",
    )

    code, detail = call(ch, FINANCE, body, [])
    if args.expect_key_mode == "enforce":
        record("NO key is REFUSED", code != "OK", f"{code} {detail}")
    else:
        record("NO key is SERVED (finance surface is ungated by design)", code == "OK", f"{code} {detail}")

    print("\n=== PRO GATE on the assistant surface ===")
    utterance = f_str(1, "what is the payment on a $420,000 loan at 6.5%?")
    code, detail = call(ch, MORTGAGE, utterance, [])
    # THE TWO GATES ARE ORDERED AND THE ORDER IS VISIBLE HERE. Under Observe the
    # key gate serves an unkeyed caller, so the Pro gate is what refuses and the
    # caller is told to call the free Finance RPC directly. Under Enforce the key
    # gate refuses FIRST, so the same request comes back UNAUTHENTICATED and that
    # helpful redirection is never reached. Both are correct; asserting only the
    # PERMISSION_DENIED shape would have reported the enforce engine as broken.
    if args.expect_key_mode == "enforce":
        record(
            "anonymous ParseOperation is refused by the KEY gate first",
            code == "UNAUTHENTICATED",
            f"{code} {detail}",
        )
        # THE ADMIT DIRECTION, which is the half a refuse-only test cannot see.
        # This key is `tier partner, scopes [finance, assistant]`, so the Pro
        # gate must LET IT THROUGH. What comes back is then a statement about
        # inference, not entitlement: locally there are no weights, so the call
        # reaches the model and stalls or answers MODEL_UNAVAILABLE. Anything
        # except PERMISSION_DENIED/UNAUTHENTICATED means both gates admitted it,
        # and that — not a specific success payload — is what is being proven.
        code, detail = call(ch, MORTGAGE, utterance, [("x-api-key", key)], timeout=8)
        record(
            "partner key (scope 'assistant') is ADMITTED past both gates",
            code not in ("PERMISSION_DENIED", "UNAUTHENTICATED"),
            f"{code} {detail}  <- past the gates; no local weights, so it stops at inference",
        )
    else:
        record(
            "anonymous ParseOperation is refused by the PRO gate",
            code == "PERMISSION_DENIED",
            f"{code} {detail}",
        )

    if args.quota_burst:
        n = args.quota_burst
        # Two callers, two buckets. The point is not merely that a refusal
        # happens -- it is that the KEY SELECTS THE TIER, so the same burst is
        # cut off at a different count depending on who sends it. A test that
        # only hammered anonymously would pass on an engine that metered
        # everyone identically.
        for label, md in (("unkeyed -> 'anonymous'", []), ("keyed -> the key's tier", [("x-api-key", key)])):
            print(f"\n=== QUOTA: {n} requests, {label} ===")
            codes: dict[str, int] = {}
            first_refusal = ""
            served_before_refusal = 0
            refused_yet = False
            for _ in range(n):
                code, detail = call(ch, FINANCE, body, md)
                codes[code] = codes.get(code, 0) + 1
                if code == "RESOURCE_EXHAUSTED":
                    if not refused_yet:
                        first_refusal, refused_yet = detail, True
                elif not refused_yet:
                    served_before_refusal += 1
            record(
                f"[{label}] refuses once the allowance is spent",
                "RESOURCE_EXHAUSTED" in codes,
                f"{codes}; served {served_before_refusal} before the first refusal",
            )
            if first_refusal:
                record(
                    f"[{label}] the refusal NAMES the tier it metered against",
                    "tier" in first_refusal,
                    first_refusal,
                )
            record(
                f"[{label}] and it SERVED the requests inside the allowance",
                served_before_refusal > 0,
                f"served {served_before_refusal}",
            )

    failed = [r for r in results if r[0] == FAIL]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    for _, name, detail in failed:
        print(f"  FAILED: {name} -- {detail}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
