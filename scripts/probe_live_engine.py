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

A CALENDAR SPREAD IS TWO LEGS, AND MULTI-LEG IS NOW PRO. PRO_GATE_MODE is
`enforce`, so an anonymous caller gets grpc-status 7 with "Multi-leg strategies
are a Pro feature" and NO message frame at all. Until 2026-11-26 this script
read that empty body as "field 15 absent" and reported OLD ENGINE -- a gate
accusing the engine of the one thing it was built to detect, while the engine
was correct and the gate was simply unauthenticated. It now reads grpc-status
FIRST and says INCOMPLETE, naming the remedy, because a gate that misreports
the cause is worse than no gate: it gets deleted, or it masks the real failure.
Pass --api-key to actually exercise the calendar path, or --skip-pro to accept
the reduced coverage deliberately.

Two parsing notes, both of which produced wrong answers here:
  - A length-delimited field's length is a VARINT. Reading it as one byte
    desynchronises the walk on the first field over 127 bytes, after which
    every later field reads as absent.
  - In proto3 a double equal to 0.0 is NOT written to the wire, so "no tag for
    field 15" and "field 15 == 0" are indistinguishable. This script reports
    the presence set rather than claiming a field is missing.

Note that `railway up` may report `operation timed out` while the deployment
nonetheless succeeds: the CLI's client deadline is shorter than the upload
endpoint's response time under load. Check the deployment list and run this
before concluding a deploy failed.
"""
import base64
import os
import struct
import sys
import urllib.parse
import urllib.request

ARGS = [a for a in sys.argv[1:]]
SKIP_PRO = "--skip-pro" in ARGS
ARGS = [a for a in ARGS if a != "--skip-pro"]

# The key is read from the argument list or the environment, never printed. It
# only ever travels as the x-api-key request header the engine reads.
API_KEY = os.environ.get("OFC_API_KEY") or ""
for i, a in enumerate(list(ARGS)):
    if a == "--api-key" and i + 1 < len(ARGS):
        API_KEY = ARGS[i + 1]
        ARGS = ARGS[:i] + ARGS[i + 2:]
        break
    if a.startswith("--api-key="):
        API_KEY = a.split("=", 1)[1]
        ARGS = [x for x in ARGS if x is not a]
        break

HOST = ARGS[0] if ARGS else "https://api.optionsandfuturescalculator.com"
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


def read_varint(buf, off):
    """Decode one varint. Returns (value, new offset)."""
    result, shift = 0, 0
    while True:
        byte = buf[off]
        off += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, off
        shift += 7


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
    headers = {
        "Content-Type": "application/grpc-web-text",
        "X-Grpc-Web": "1",
        "Accept": "application/grpc-web-text",
    }
    if API_KEY:
        headers["x-api-key"] = API_KEY
    r = urllib.request.Request(
        HOST + METHOD,
        data=base64.b64encode(frame),
        headers=headers,
    )
    resp = urllib.request.urlopen(r, timeout=60)
    # A gRPC-Web call that fails before producing a message returns 200 with an
    # EMPTY body and grpc-status in the HTTP HEADERS, not in a trailer frame.
    # Reading only the body makes a refusal look like a missing field.
    hdrs = {k.lower(): v for k, v in resp.getheaders()}
    raw = b"".join(resp.read().split())

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

    # grpc-status may arrive in the headers (trailers-only response) or in the
    # trailer frame. Prefer whichever is present; "" means neither said.
    status = hdrs.get("grpc-status", "")
    message = hdrs.get("grpc-message", "")
    for part in trailers.replace("\r\n", "\n").split("\n"):
        if part.lower().startswith("grpc-status:"):
            status = part.split(":", 1)[1].strip()
        elif part.lower().startswith("grpc-message:"):
            message = part.split(":", 1)[1].strip()

    found = {
        "trailers": trailers,
        "grpc_status": status,
        "grpc_message": urllib.parse.unquote(message) if message else "",
        "bytes": len(msg),
        "present": set(),
    }
    off = 0
    while off < len(msg):
        tagval, off = read_varint(msg, off)
        field, wire = tagval >> 3, tagval & 7
        found["present"].add(field)
        if wire == 1:
            if field in WANT:
                found[WANT[field]] = struct.unpack("<d", msg[off:off + 8])[0]
            off += 8
        elif wire == 2:
            # LENGTH IS A VARINT. Reading it as a single byte desynchronises
            # the walk on the first field over 127 bytes (pnl_matrix and matrix
            # both exceed it), after which every later field reads as absent.
            ln, off = read_varint(msg, off)
            body = msg[off:off + ln]
            if field == 6 and ln >= 9 and body[0] == 0x09:      # Greeks.delta
                found["delta"] = struct.unpack("<d", body[1:9])[0]
            off += ln
        elif wire == 0:
            _, off = read_varint(msg, off)
        elif wire == 5:
            off += 4
        else:
            break
    return found


failures = []

# 1. Calendar spread: per-leg clocks, curve drawn at the near expiry.
#    Two legs is a Pro position, so READ grpc-status BEFORE reading fields: an
#    empty body from a refusal is not evidence about the engine's build.
cal = call()
gated = False
print(f"{'grpc-status':26s}: {cal['grpc_status'] or '(none — message returned)'}")
if cal["grpc_message"]:
    print(f"{'grpc-message':26s}: {cal['grpc_message']}")
print(f"{'message bytes':26s}: {cal['bytes']}")
print(f"{'fields present':26s}: {sorted(cal['present']) or '(none)'}")
print("trailers:", cal["trailers"] or "(none)")
for k in ("curve_days_to_expiration", "max_profit", "max_loss"):
    print(f"{k:26s}: {cal.get(k, 'not on the wire (absent OR 0.0)')}")

if cal["grpc_status"] == "7":
    # PERMISSION_DENIED. With a key this is a real failure; without one it is
    # the gate doing its job and the calendar check simply cannot run.
    if API_KEY:
        failures.append(
            "PERMISSION_DENIED with a key supplied — the key does not carry Pro: "
            + (cal["grpc_message"] or "no message"))
    else:
        gated = True
        print("\nCALENDAR CHECKS SKIPPED: the Pro gate refused this 2-leg call "
              "(grpc-status 7).\n  This says NOTHING about the engine build. "
              "Re-run with --api-key <key> to verify the\n  calendar path, or "
              "--skip-pro to accept the reduced coverage.")
elif cal["grpc_status"] not in ("", "0"):
    failures.append(
        f"grpc-status {cal['grpc_status']} on the calendar call: "
        + (cal["grpc_message"] or "no message"))
else:
    if 15 not in cal["present"]:
        failures.append("field 15 not on the wire — OLD ENGINE, or the curve is at 0d")
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
if gated and not SKIP_PRO:
    # Distinct from FAILED on purpose. Nothing is known to be wrong, and the
    # calendar half of the gate did not run -- exiting 0 here would report
    # coverage this run does not have.
    print("\nVERDICT: INCOMPLETE — dividends priced and the engine answers, but the"
          "\n  calendar checks never ran (Pro gate). Pass --api-key to verify them,"
          "\n  or --skip-pro to accept this as a pass.")
    sys.exit(3)
if gated:
    print("\nVERDICT: LIVE (REDUCED) — dividends priced; calendar checks skipped by --skip-pro")
    sys.exit(0)
print("\nVERDICT: LIVE — near-expiry curve, real calendar shape, dividends priced")
