#!/usr/bin/env python3
"""Pro-gate matrix over gRPC-Web, against the real deployed endpoint.

    LICENCE_SIGNING_KEY=... SUPABASE_JWT_SECRET=... \
        scripts/probe_pro_gate_web.py [https://api.optionsandfuturescalculator.com]

Why this exists alongside probe_pro_gate.sh: that one drives smoke_client over
NATIVE gRPC, which does not survive the Railway ingress (see CLAUDE.md). Pointed
at production it cannot connect, and -- because it collapses every non-zero exit
to "deny" -- an unreachable engine renders as a near-perfect PASS column. Ten
rows "passed" against an endpoint that was never listening. gRPC-Web is the only
transport that reaches the container, so it is the only one that can verify the
deployed gate.

Two rules follow from that failure, and they are the point of this file:

  1. A refusal is only a refusal when the engine SAYS so. grpc-status 7
     (PERMISSION_DENIED) is a deny. A connection error, a 5xx, or status 14
     (UNAVAILABLE) is an ERROR -- never scored as a deny.

  2. A liveness control runs FIRST: an anonymous SINGLE-leg call, which the free
     tier must allow. If that does not come back allowed, every deny below is
     unfalsifiable, so the run aborts as INVALID instead of printing passes.

Exits non-zero if any row disagrees, so it works as a release gate.

@author Olumuyiwa Oluwasanmi
"""
import base64
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request

HOST = (sys.argv[1] if len(sys.argv) > 1 else "https://api.optionsandfuturescalculator.com").rstrip("/")
METHOD = "/calculator.OptionsCalculator/CalculateStrategy"
REPO = pathlib.Path(__file__).resolve().parent.parent
LICENCE_TS = REPO / "workers" / "billing" / "src" / "licence.ts"
MINT = REPO / "scripts" / "mint_pro_gate_creds.mjs"


def die(msg):
    print(f"probe_pro_gate_web: {msg}", file=sys.stderr)
    sys.exit(2)


# --- protobuf encoding (same shapes as probe_live_engine.py) ----------------

def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        out += bytes([b | (0x80 if n else 0)])
        if not n:
            return out


def tag(field, wire):
    return varint((field << 3) | wire)


def dbl(field, v):
    return tag(field, 1) + struct.pack("<d", v)


def i32(field, v):
    return tag(field, 0) + varint(v)


def s(field, v):
    b = v.encode()
    return tag(field, 2) + varint(len(b)) + b


def sub(field, b):
    return tag(field, 2) + varint(len(b)) + b


STRIKE, PREMIUM, IV, SPOT = 739.0, 6.26, 0.147, 739.0


def leg(action, dte, strike=STRIKE, opt_type=0):
    return (
        i32(1, action)          # action: 0 BUY, 1 SELL
        + i32(2, opt_type)      # type: 0 CALL, 1 PUT
        + dbl(3, strike)
        + dbl(4, dte)
        + i32(5, 1)             # quantity
        + dbl(6, PREMIUM)
        + dbl(7, IV)
        + dbl(8, 100.0)         # contract multiplier
    )


def build_request(legs):
    req = s(1, "SPY") + dbl(2, SPOT) + dbl(3, IV) + dbl(4, 0.05) + dbl(9, 0.0)
    for l in legs:
        req += sub(5, l)
    return req


# A four-leg iron-condor-shaped position: the multi-leg class the gate covers.
FOUR_LEG = [
    leg(0, 30.0, STRIKE - 20.0, opt_type=1),   # BUY  put  wing
    leg(1, 30.0, STRIKE - 10.0, opt_type=1),   # SELL put
    leg(1, 30.0, STRIKE + 10.0, opt_type=0),   # SELL call
    leg(0, 30.0, STRIKE + 20.0, opt_type=0),   # BUY  call wing
]
ONE_LEG = [leg(0, 30.0)]


# --- transport --------------------------------------------------------------

def call(legs, bearer=None, licence=None, timeout=90):
    """One CalculateStrategy round trip over gRPC-Web.

    Returns ('allow', detail) | ('deny', detail) | ('error', detail).
    'error' is deliberately NOT 'deny' -- conflating them is what made the
    native-gRPC probe report passes against a dead endpoint.
    """
    req = build_request(legs)
    frame = b"\x00" + struct.pack(">I", len(req)) + req
    headers = {
        "Content-Type": "application/grpc-web-text",
        "Accept": "application/grpc-web-text",
        "X-Grpc-Web": "1",
    }
    if bearer:
        headers["authorization"] = f"Bearer {bearer}"
    if licence:
        # A subscription licence rides the SAME channel as an API key -- see
        # api_key.cpp's `key.starts_with(kLicencePrefix)` branch. There is no
        # separate "licence" header; sending one gets it silently ignored and
        # the caller scored as anonymous, which reads exactly like a gate bug.
        headers["x-api-key"] = licence

    r = urllib.request.Request(HOST + METHOD, data=base64.b64encode(frame), headers=headers)
    try:
        resp = urllib.request.urlopen(r, timeout=timeout)
        raw = resp.read()
        status = resp.headers.get("grpc-status")
        message = resp.headers.get("grpc-message") or ""
    except urllib.error.HTTPError as e:
        return "error", f"HTTP {e.code}: {e.read()[:160].decode(errors='replace')}"
    except Exception as e:  # noqa: BLE001 -- any transport failure is an error row
        return "error", f"transport: {type(e).__name__}: {e}"

    # A trailers-only response carries grpc-status in the HTTP headers with no
    # body; reading only frames misses it entirely.
    if status is None:
        status, message = _status_from_trailers(raw)

    if status is None:
        # Frames came back with no status anywhere -- if there is a real
        # response message, the call was served.
        return ("allow", "no grpc-status, response body present") if raw else \
               ("error", "no grpc-status and no body")

    code = int(status)
    if code == 0:
        return "allow", "grpc-status:0"
    if code == 7:
        return "deny", f"grpc-status:7 PERMISSION_DENIED {message}".strip()
    if code == 8:
        return "error", f"grpc-status:8 RESOURCE_EXHAUSTED (quota, not the gate) {message}".strip()
    return "error", f"grpc-status:{code} {message}".strip()


def _status_from_trailers(raw):
    """Pull grpc-status out of the trailer frame of a grpc-web-text body."""
    blob = b""
    for chunk in raw.split(b"="):
        if not chunk:
            continue
        pad = chunk + b"=" * (-len(chunk) % 4)
        try:
            blob += base64.b64decode(pad)
        except Exception:  # noqa: BLE001
            pass
    i = 0
    while i + 5 <= len(blob):
        flag = blob[i]
        ln = struct.unpack(">I", blob[i + 1:i + 5])[0]
        payload = blob[i + 5:i + 5 + ln]
        i += 5 + ln
        if flag & 0x80:  # trailer frame
            status, message = None, ""
            for line in payload.split(b"\r\n"):
                if line.lower().startswith(b"grpc-status:"):
                    status = line.split(b":", 1)[1].strip().decode()
                elif line.lower().startswith(b"grpc-message:"):
                    message = line.split(b":", 1)[1].strip().decode(errors="replace")
            return status, message
    return None, ""


# --- credentials ------------------------------------------------------------

def mint(workdir):
    if not LICENCE_TS.is_file():
        die(f"missing {LICENCE_TS}")
    if not MINT.is_file():
        die(f"missing {MINT}")
    for var in ("LICENCE_SIGNING_KEY", "SUPABASE_JWT_SECRET"):
        if not os.environ.get(var):
            die(f"{var} is unset -- it must match the engine's")
    proc = subprocess.run(
        ["node", str(MINT), workdir, str(LICENCE_TS)],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        die(f"could not mint test credentials:\n{proc.stderr.strip()}")


def main():
    work = tempfile.mkdtemp()
    mint(work)
    read = lambda n: (pathlib.Path(work) / n).read_text().strip()  # noqa: E731

    print(f"Pro-gate matrix over gRPC-Web against {HOST}")
    print()

    # --- Liveness control. Everything below is meaningless without it. ------
    verdict, detail = call(ONE_LEG)
    if verdict != "allow":
        print(f"  INVALID  anonymous SINGLE-leg control  expected allow got {verdict}")
        print(f"           | {detail}")
        print()
        print("  The free tier is not answering, so no 'deny' below would prove")
        print("  anything -- an unreachable engine denies everything. Not scoring.")
        return 2
    print(f"  CONTROL  anonymous single-leg allowed      ({detail})")
    print()

    rows = [
        ("signed-in, app_metadata.tier=pro",          "allow", "jwt", "jwt.pro"),
        ("signed-in, app_metadata.tier=free",         "deny",  "jwt", "jwt.free"),
        ("user_metadata.tier=pro (browser-writable)",  "deny",  "jwt", "jwt.usermeta"),
        ("JWT payload edited after signing",           "deny",  "jwt", "jwt.tampered"),
        ("expired JWT, tier=pro",                      "deny",  "jwt", "jwt.expired"),
        ("alg:none, tier=pro, no signature",           "deny",  "jwt", "jwt.algnone"),
        ("garbage bearer token",                       "deny",  "jwt", "jwt.garbage"),
        ("anonymous (no credential)",                  "deny",  "none", None),
        ("valid Pro licence",                          "allow", "lic", "lic.valid"),
        ("expired licence",                            "deny",  "lic", "lic.expired"),
        ("licence payload edited after signing",       "deny",  "lic", "lic.tampered"),
        ("licence signed with the wrong key",          "deny",  "lic", "lic.wrongkey"),
        ("garbage licence",                            "deny",  "lic", "lic.garbage"),
    ]

    passed = failed = errored = 0
    for name, expect, kind, fname in rows:
        kwargs = {}
        if kind == "jwt":
            kwargs["bearer"] = read(fname)
        elif kind == "lic":
            kwargs["licence"] = read(fname)
        verdict, detail = call(FOUR_LEG, **kwargs)

        if verdict == "error":
            print(f"  ERROR {name:<44s} expected {expect:<5s} got error")
            print(f"          | {detail}")
            errored += 1
        elif verdict == expect:
            print(f"  PASS  {name:<44s} expected {expect:<5s} got {verdict}")
            passed += 1
        else:
            print(f"  FAIL  {name:<44s} expected {expect:<5s} got {verdict}")
            print(f"          | {detail}")
            failed += 1

    print()
    print(f"  passed {passed}, failed {failed}, errored {errored}")
    return 0 if (failed == 0 and errored == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
