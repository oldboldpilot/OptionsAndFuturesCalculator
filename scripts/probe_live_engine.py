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

req = (
    s(1, "SPY")
    + dbl(2, SPOT)
    + dbl(3, IV)
    + dbl(4, 0.05)
    + sub(5, leg(1, 30.0, STRIKE, PREMIUM, IV))   # SELL near
    + sub(5, leg(0, 60.0, STRIKE, PREMIUM, IV))   # BUY far
)

frame = b"\x00" + struct.pack(">I", len(req)) + req
body = base64.b64encode(frame)

r = urllib.request.Request(
    HOST + METHOD,
    data=body,
    headers={
        "Content-Type": "application/grpc-web-text",
        "X-Grpc-Web": "1",
        "Accept": "application/grpc-web-text",
    },
)
raw = urllib.request.urlopen(r, timeout=60).read()
raw = b"".join(raw.split())  # strip any newlines

# grpc-web-text base64-encodes each frame SEPARATELY and concatenates the
# results, so each segment carries its own '=' padding and the whole string is
# not one valid base64 document. Split after each padding run and decode each
# segment on its own.
segments, start = [], 0
i = 0
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

data = b""
for seg in segments:
    data += base64.b64decode(seg + b"=" * (-len(seg) % 4))

# Walk the gRPC-Web frames; the message frame has flag 0, trailers have 0x80.
msg, off = b"", 0
while off + 5 <= len(data):
    flags = data[off]
    ln = struct.unpack(">I", data[off + 1: off + 5])[0]
    payload = data[off + 5: off + 5 + ln]
    if flags == 0:
        msg = payload
    else:
        sys.stdout.write("trailers: " + payload.decode(errors="replace").strip() + "\n")
    off += 5 + ln

# Pull the doubles we care about out of the response.
want = {1: "max_profit", 2: "max_loss", 15: "curve_days_to_expiration"}
found = {}
off = 0
while off < len(msg):
    key = msg[off]
    field, wire = key >> 3, key & 7
    off += 1
    if wire == 1:
        if field in want:
            found[want[field]] = struct.unpack("<d", msg[off:off + 8])[0]
        off += 8
    elif wire == 2:
        ln = msg[off]
        off += 1 + ln
    elif wire == 0:
        while msg[off] & 0x80:
            off += 1
        off += 1
    else:
        break

print(f"response bytes : {len(msg)}")
for k in ("curve_days_to_expiration", "max_profit", "max_loss"):
    print(f"{k:26s}: {found.get(k, 'ABSENT')}")

if "curve_days_to_expiration" not in found:
    print("\nVERDICT: OLD ENGINE — field 15 absent")
    sys.exit(1)
if abs(found["curve_days_to_expiration"] - 30.0) > 1e-6:
    print(f"\nVERDICT: curve drawn at {found['curve_days_to_expiration']}d, expected the near 30d")
    sys.exit(1)
if found.get("max_profit", 0) - found.get("max_loss", 0) < 1.0:
    print("\nVERDICT: calendar P&L still flat — single clock")
    sys.exit(1)
print("\nVERDICT: NEW ENGINE LIVE — curve at near expiry, calendar has real shape")
