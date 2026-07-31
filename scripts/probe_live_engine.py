#!/usr/bin/env python3
"""Verify the DEPLOYED engine over gRPC-Web, with a calendar spread.

    python3 scripts/probe_live_engine.py [https://host]

Exits non-zero if the deployed engine is the old build or prices a calendar
spread on a single clock, so it works as a post-deploy gate.

This exists because nothing else could check the deployed engine.
backend/src/smoke_client.cpp speaks native gRPC and the public endpoint is
gRPC-Web behind Envoy, so the smoke client can only ever test a local engine.
Everything else available here checks HTTP status codes, which stay 200 across
a wrong build.

The protobuf is hand-encoded rather than generated: adding a Python codegen
step to a C++/TypeScript repo for one diagnostic is a worse trade than forty
lines of struct.pack. The discriminator is
StrategyResponse.curve_days_to_expiration (field 15) — absent from any build
before 2026-07-30, so its presence proves which engine answered, and its value
proves the payoff curve is drawn at the near expiry rather than the horizon.

Note that `railway up` may report `operation timed out` while the deployment
nonetheless succeeds: the CLI's client deadline is shorter than the upload
endpoint's response time under load. Check the deployment list and run this
before concluding a deploy failed.
"""
import base64
import struct
import sys
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "https://api.optionsandfuturescalculator.com"
METHOD = "/calculator.OptionsCalculator/CalculateStrategy"


def tag(field, wire):
    return varint((field << 3) | wire)


def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        out += bytes([b | (0x80 if n else 0)])
        if not n:
            return out


def dbl(field, v):
    return tag(field, 1) + struct.pack("<d", v)


def i32(field, v):
    return tag(field, 0) + varint(v)


def s(field, v):
    b = v.encode()
    return tag(field, 2) + varint(len(b)) + b


def sub(field, b):
    return tag(field, 2) + varint(len(b)) + b


def leg(action, dte, strike, premium, iv):
    return (
        i32(1, action)      # action: 0 BUY, 1 SELL
        + i32(2, 0)         # type: CALL
        + dbl(3, strike)
        + dbl(4, dte)
        + i32(5, 1)         # quantity
        + dbl(6, premium)
        + dbl(7, iv)
        + dbl(8, 100.0)     # contract multiplier
    )


STRIKE, PREMIUM, IV, SPOT = 739.0, 6.26, 0.147, 739.0

# Response fields we read back. Greeks is a nested message (field 6); delta is
# its field 1.
WANT = {1: "max_profit", 2: "max_loss", 15: "curve_days_to_expiration"}


def call(dividend_yield=0.0, calendar=True):
    """One CalculateStrategy round trip. Returns the decoded doubles."""
    req = (
        s(1, "SPY")
        + dbl(2, SPOT)
        + dbl(3, IV)
        + dbl(4, 0.05)
        + dbl(9, dividend_yield)
    )
    if calendar:
        req += sub(5, leg(1, 30.0, STRIKE, PREMIUM, IV))   # SELL near
        req += sub(5, leg(0, 60.0, STRIKE, PREMIUM, IV))   # BUY far
    else:
        req += sub(5, leg(0, 30.0, STRIKE, PREMIUM, IV))   # plain long call

    frame = b"\x00" + struct.pack(">I", len(req)) + req
    r = urllib.request.Request(
        HOST + METHOD,
        data=base64.b64encode(frame),
        headers={
            "Content-Type": "application/grpc-web-text",
            "X-Grpc-Web": "1",
            "Accept": "application/grpc-web-text",
        },
    )
    raw = b"".join(urllib.request.urlopen(r, timeout=60).read().split())

    # grpc-web-text base64-encodes each frame SEPARATELY and concatenates the
    # results, so each segment carries its own '=' padding and the whole string
    # is not one valid base64 document. Split after each padding run.
    segments, start, i = [], 0, 0
    while i < len(raw):
        if raw[i:i + 1] == b"=":
            while i < len(raw) and raw[i:i + 1] == b"=":
                i += 1
            segments.append(raw[start:i])
            start = i
        else:
            i += 1
    if start < len(raw):
        segments.append(raw[start:])
    data = b"".join(base64.b64decode(sg + b"=" * (-len(sg) % 4)) for sg in segments)

    # Walk the gRPC-Web frames; the message frame has flag 0, trailers have 0x80.
    msg, off, trailers = b"", 0, ""
    while off + 5 <= len(data):
        flags = data[off]
        ln = struct.unpack(">I", data[off + 1:off + 5])[0]
        payload = data[off + 5:off + 5 + ln]
        if flags == 0:
            msg = payload
        else:
            trailers = payload.decode(errors="replace").strip()
        off += 5 + ln

    found = {"trailers": trailers}
    off = 0
    while off < len(msg):
        key = msg[off]
        field, wire = key >> 3, key & 7
        off += 1
        if wire == 1:
            if field in WANT:
                found[WANT[field]] = struct.unpack("<d", msg[off:off + 8])[0]
            off += 8
        elif wire == 2:
            ln = msg[off]
            body = msg[off + 1:off + 1 + ln]
            if field == 6 and ln >= 9 and body[0] == 0x09:      # Greeks.delta
                found["delta"] = struct.unpack("<d", body[1:9])[0]
            off += 1 + ln
        elif wire == 0:
            while msg[off] & 0x80:
                off += 1
            off += 1
        else:
            break
    found["bytes"] = len(msg)
    return found


failures = []

# 1. Calendar spread: per-leg clocks, curve drawn at the near expiry.
cal = call()
print("trailers:", cal["trailers"])
for k in ("curve_days_to_expiration", "max_profit", "max_loss"):
    print(f"{k:26s}: {cal.get(k, 'ABSENT')}")

if "curve_days_to_expiration" not in cal:
    failures.append("field 15 absent — OLD ENGINE")
elif abs(cal["curve_days_to_expiration"] - 30.0) > 1e-6:
    failures.append(f"curve drawn at {cal['curve_days_to_expiration']}d, expected the near 30d")
if cal.get("max_profit", 0) - cal.get("max_loss", 0) < 1.0:
    failures.append("calendar P&L still flat — legs on a single clock")

# 2. Dividend yield must reach the pricing path. A long call on a
#    dividend-paying underlying has a lower forward, so its delta must fall.
#    q = 0 must reproduce the dividend-free answer exactly.
z0 = call(0.0, calendar=False)
z1 = call(0.0, calendar=False)
dq = call(0.04, calendar=False)
d0, dd = z0.get("delta"), dq.get("delta")
print(f"{'delta q=0.00':26s}: {d0}")
print(f"{'delta q=0.04':26s}: {dd}")

if d0 is None or dd is None:
    failures.append("could not read net_greeks.delta")
else:
    if z0.get("delta") != z1.get("delta"):
        failures.append("q=0 is not deterministic")
    if not dd < d0:
        failures.append(f"dividend yield did not reduce delta ({dd} >= {d0}) — q ignored")
    elif dd < d0 * 0.5:
        failures.append(f"dividend-adjusted delta collapsed ({dd} vs {d0}) — q looks mis-scaled")

if failures:
    print("\nVERDICT: FAILED")
    for f in failures:
        print("  - " + f)
    sys.exit(1)
print("\nVERDICT: LIVE — near-expiry curve, real calendar shape, dividends priced")
