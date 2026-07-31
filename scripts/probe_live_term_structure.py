#!/usr/bin/env python3
"""Verify the DEPLOYED engine serves a futures term structure, over gRPC-Web.

    python3 scripts/probe_live_term_structure.py [https://host]

Exits non-zero if the deployed build predates the term structure, prices a
curve off the wrong underlying, or fabricates order-book fields.

Why this exists separately from probe_live_engine.py: that script exercises
CalculateStrategy, and the term structure is served by GetMarketChain. Both
land in the same binary, but a passing CalculateStrategy probe says nothing
about whether the chain path was in the deployed build — which is exactly the
mistake this file is here to make impossible.

The three things checked are the three ways this feature has actually been
wrong:

  1. ABSENT     — the deployed build predates the curve, so the panel renders
                  empty and looks like a frontend bug.
  2. WRONG LEVEL— 'ES' resolved to Eversource Energy (~71) rather than the
                  E-mini S&P index level (~7400). Arithmetically flawless,
                  economically nonsense. A floor on the index level catches it.
  3. FABRICATED — bid/ask/volume/open interest are order-book facts. No formula
                  produces them. A derived curve that fills them in is the
                  invention spec 3.4 forbids, so they must stay zero.

Unmapped roots must REFUSE rather than emit a curve at an equity's price.
"""
import base64
import struct
import sys
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "https://api.optionsandfuturescalculator.com"
METHOD = "/calculator.OptionsCalculator/GetMarketChain"
QUOTE_METHOD = "/calculator.OptionsCalculator/GetMarketQuote"


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


def s(field, v):
    b = v.encode()
    return tag(field, 2) + varint(len(b)) + b


def i32(field, v):
    return tag(field, 0) + varint(v)


def read_varint(buf, off):
    n, shift = 0, 0
    while True:
        b = buf[off]
        off += 1
        n |= (b & 0x7F) << shift
        if not b & 0x80:
            return n, off
        shift += 7


def fields(buf):
    """Yield (field_number, wire_type, value) for one protobuf message.

    Values arrive as int for varints, float for 64-bit, bytes for delimited.
    Enough of the wire format to read a response; not a protobuf library.
    """
    off = 0
    while off < len(buf):
        key, off = read_varint(buf, off)
        field, wire = key >> 3, key & 7
        if wire == 0:
            v, off = read_varint(buf, off)
            yield field, wire, v
        elif wire == 1:
            yield field, wire, struct.unpack("<d", buf[off:off + 8])[0]
            off += 8
        elif wire == 2:
            ln, off = read_varint(buf, off)
            yield field, wire, buf[off:off + ln]
            off += ln
        elif wire == 5:
            yield field, wire, struct.unpack("<f", buf[off:off + 4])[0]
            off += 4
        else:
            return


def invoke(method, req):
    """One gRPC-Web round trip. Returns (trailers, message bytes)."""
    frame = b"\x00" + struct.pack(">I", len(req)) + req
    r = urllib.request.Request(
        HOST + method,
        data=base64.b64encode(frame),
        headers={
            "Content-Type": "application/grpc-web-text",
            "X-Grpc-Web": "1",
            "Accept": "application/grpc-web-text",
        },
    )
    raw = b"".join(urllib.request.urlopen(r, timeout=90).read().split())

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
    return trailers, msg


def call(symbol, asset_class="FUTURES"):
    """GetMarketChain. Returns (trailers, decoded response)."""
    trailers, msg = invoke(METHOD, s(1, symbol) + i32(2, 30) + s(3, asset_class))

    out = {"spot": 0.0, "strikes": 0, "contracts": [], "provider": ""}
    for field, _wire, v in fields(msg):
        if field == 2:
            out["spot"] = v
        elif field == 3:
            out["strikes"] += 1
        elif field == 4:
            c = {"code": "", "delivery": "", "days": 0, "fwd": 0.0, "bid": 0.0,
                 "ask": 0.0, "basis": 0.0, "carry": 0.0, "vol": 0, "oi": 0,
                 "state": ""}
            names = {1: "code", 2: "delivery", 3: "days", 4: "fwd", 5: "bid",
                     6: "ask", 7: "basis", 8: "carry", 9: "vol", 10: "oi",
                     11: "state"}
            for f2, _w2, v2 in fields(v):
                if f2 in names:
                    c[names[f2]] = v2.decode() if isinstance(v2, bytes) else v2
            out["contracts"].append(c)
        elif field == 7:
            out["provider"] = v.decode()
    return trailers, out


def quote(symbol, asset_class):
    """GetMarketQuote. Returns (trailers, {price, provider, asset_class})."""
    trailers, msg = invoke(QUOTE_METHOD, s(1, symbol) + s(2, asset_class))
    out = {"price": 0.0, "provider": "", "asset_class": ""}
    names = {2: "price", 6: "asset_class", 7: "provider"}
    for field, _wire, v in fields(msg):
        if field in names:
            out[names[field]] = v.decode() if isinstance(v, bytes) else v
    return trailers, out


failures = []

# Roots the engine maps to an index proxy, with the floor each curve must clear.
# The floor is what separates the E-mini S&P from the NYSE-listed utility that
# happens to share its ticker.
MAPPED = {"ES": 3000.0, "NQ": 10000.0}
# Roots with no proxy. These must refuse, not quote an equity's price as a
# commodity future.
UNMAPPED = ["GC", "CL", "NG", "SI", "ZB"]

for sym, floor in MAPPED.items():
    try:
        trailers, r = call(sym)
    except Exception as exc:                                # noqa: BLE001
        failures.append(f"{sym}: request failed: {exc}")
        continue

    cs = r["contracts"]
    print(f"\n{sym}  spot={r['spot']:.2f}  contracts={len(cs)}  "
          f"provider={r['provider']}  [{trailers}]")

    if "grpc-status:0" not in trailers.replace(" ", ""):
        failures.append(f"{sym}: {trailers}")
        continue
    if not cs:
        failures.append(f"{sym}: no futures contracts — deployed build predates "
                        f"the term structure")
        continue
    if r["spot"] < floor:
        failures.append(f"{sym}: spot {r['spot']:.2f} below the index floor "
                        f"{floor:.0f} — priced off the equity of the same ticker")

    for c in cs:
        print(f"  {c['code']:8s} {c['delivery']:8s} {c['days']:4d}d  "
              f"fwd={c['fwd']:10.2f}  basis={c['basis']:+8.2f}  "
              f"carry={c['carry'] * 100:.4f}%  [{c['state']}]")

        # Basis is definitionally forward minus spot. If it ever disagrees the
        # two came from different places and one of them is wrong.
        if abs((c["fwd"] - r["spot"]) - c["basis"]) > 0.01:
            failures.append(f"{sym} {c['code']}: basis {c['basis']:.4f} != "
                            f"forward - spot {c['fwd'] - r['spot']:.4f}")
        # Derived figures must say so.
        if c["state"] != "MODELLED":
            failures.append(f"{sym} {c['code']}: state {c['state']!r}, expected "
                            f"MODELLED — a derived curve must be labelled")
        # Order-book fields have no formula. Zero is the honest answer.
        for k in ("bid", "ask", "vol", "oi"):
            if c[k]:
                failures.append(f"{sym} {c['code']}: {k}={c[k]} — fabricated "
                                f"order-book data")

    # Contango under positive carry: the basis must widen with tenor, and the
    # contracts must arrive in tenor order for the panel to read as a curve.
    days = [c["days"] for c in cs]
    if days != sorted(days):
        failures.append(f"{sym}: contracts out of tenor order: {days}")
    if len(cs) >= 2 and not cs[-1]["basis"] > cs[0]["basis"]:
        failures.append(f"{sym}: basis does not widen with tenor "
                        f"({cs[0]['basis']:.2f} -> {cs[-1]['basis']:.2f})")

    # The quote path must describe the SAME instrument as the curve.
    #
    # This is the check that was missing, and its absence shipped a build where
    # every assertion above passed while the header read "ES spot 71.59" over a
    # 7500 curve. Both were real quotes; one was Eversource Energy, the utility
    # that owns the ticker. The spot is what prices every leg on the screen, so
    # it mattered more than the curve did.
    try:
        qt, q = quote(sym, "FUTURES")
    except Exception as exc:                                # noqa: BLE001
        failures.append(f"{sym}: FUTURES quote failed: {exc}")
        continue
    print(f"  quote {q['price']:.2f}  class={q['asset_class']}  via {q['provider']}")
    if q["price"] <= 0 or abs(q["price"] - r["spot"]) / r["spot"] > 0.01:
        failures.append(f"{sym}: quote {q['price']:.2f} disagrees with chain spot "
                        f"{r['spot']:.2f} — quote path and curve are on "
                        f"different instruments")
    if "proxy" not in q["provider"]:
        failures.append(f"{sym}: quote provider {q['provider']!r} does not "
                        f"disclose that the level is derived")

    # The same ticker asked as an EQUITY must still answer as one. If both
    # classes return the same number, asset_class is being ignored and the
    # agreement above proves nothing.
    # Not every futures root has an equity twin — NQ has none, and the lookup
    # simply fails. That is not evidence either way, so it is reported as what
    # it is rather than counted as a passing distinction.
    try:
        _, eq = quote(sym, "EQUITY")
        if eq["price"] <= 0:
            print(f"  as EQUITY: no such listed ticker (nothing to confuse it with)")
        elif abs(eq["price"] - q["price"]) < 0.01:
            failures.append(f"{sym}: identical price as EQUITY and FUTURES — "
                            f"asset_class is not routing")
        else:
            print(f"  as EQUITY {eq['price']:.2f} — a different instrument")
    except Exception:                                       # noqa: BLE001
        print(f"  as EQUITY: refused (no such listed ticker)")

for sym in UNMAPPED:
    try:
        trailers, r = call(sym)
        ok = bool(r["contracts"])
    except Exception:                                       # noqa: BLE001
        ok, trailers, r = False, "request refused", {"contracts": [], "spot": 0}
    status = "quoted a curve" if ok else "refused"
    print(f"\n{sym}  {status}  spot={r['spot']:.2f}  contracts={len(r['contracts'])}")
    if ok:
        failures.append(f"{sym}: emitted a curve with no index proxy — this is "
                        f"an equity's price dressed as a commodity future")

    # Refusing in the chain while the quote path serves the equity is exactly
    # the split that shipped. Both have to refuse.
    try:
        _, q = quote(sym, "FUTURES")
        if q["price"] > 0:
            failures.append(f"{sym}: FUTURES quote returned {q['price']:.2f} with "
                            f"no futures source mapped — that is the listed equity")
    except Exception:                                       # noqa: BLE001
        pass

print()
if failures:
    for f in failures:
        print("FAIL:", f)
    sys.exit(1)
print("VERDICT: LIVE — term structure served, index-level, labelled MODELLED, "
      "no fabricated book")
