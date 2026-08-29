#!/usr/bin/env python3
"""Exercise sensen.finance.Finance over gRPC-Web and check the answers.

    python3 scripts/probe_finance_service.py [https://host | http://localhost:8080]

Every case here is checked against a value derived INDEPENDENTLY of the engine
-- a closed-form formula evaluated in Python, or an identity the answer must
satisfy -- not against a number the engine produced earlier. A gate that
compares the engine to its own last output only detects a crash.

The protobuf is hand-encoded for the same reason scripts/probe_live_engine.py
does it: adding a Python codegen step to a C++/TypeScript repo for one
diagnostic costs more than the forty lines below.

Decimal fields cross the wire as exact strings, because sensen's BigDecimal is
a genuine 18-decimal-place fixed-point type and double would quietly truncate
it -- so the checks below parse them with Decimal, not float.
"""
import base64
import json
import math
import re
import struct
import sys
import urllib.request
from decimal import Decimal

HOST = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8080"
SVC = "/sensen.finance.Finance/"


# --------------------------------------------------------------------------
# protobuf wire helpers
# --------------------------------------------------------------------------

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


def dbl(field, v):
    return tag(field, 1) + struct.pack("<d", v)


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
    off = 0
    while off < len(buf):
        key, off = read_varint(buf, off)
        field, wire = key >> 3, key & 7
        if wire == 0:
            v, off = read_varint(buf, off)
            yield field, v
        elif wire == 1:
            yield field, struct.unpack("<d", buf[off:off + 8])[0]
            off += 8
        elif wire == 2:
            ln, off = read_varint(buf, off)
            yield field, buf[off:off + ln]
            off += ln
        elif wire == 5:
            yield field, struct.unpack("<f", buf[off:off + 4])[0]
            off += 4
        else:
            return


def call(method, req):
    """One gRPC-Web round trip. Returns (trailers, message bytes)."""
    frame = b"\x00" + struct.pack(">I", len(req)) + req
    r = urllib.request.Request(
        HOST + SVC + method,
        data=base64.b64encode(frame),
        headers={
            "Content-Type": "application/grpc-web-text",
            "X-Grpc-Web": "1",
            "Accept": "application/grpc-web-text",
        },
    )
    raw = b"".join(urllib.request.urlopen(r, timeout=90).read().split())

    # grpc-web-text base64-encodes each frame separately and concatenates, so
    # each segment carries its own padding and the whole string is not one
    # valid base64 document. Split after each padding run.
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


def one_string(msg, field=1):
    for f, v in fields(msg):
        if f == field and isinstance(v, bytes):
            return v.decode()
    return None


def one_double(msg, field=1):
    for f, v in fields(msg):
        if f == field:
            return v
    return None


failures = []
checks = 0


def check(name, ok, detail):
    global checks
    checks += 1
    print(f"  {'PASS' if ok else 'FAIL'}  {name}: {detail}")
    if not ok:
        failures.append(f"{name}: {detail}")


# --------------------------------------------------------------------------
# 1. Time value of money -- against the closed-form annuity formula.
# --------------------------------------------------------------------------
print("\nTime value of money")

# A 30-year 300,000 loan at 6% nominal. Monthly rate 0.005, 360 periods.
RATE = Decimal("0.005")
N = 360
PV = Decimal("300000")
_, msg = call("ComputePayment", s(1, str(RATE)) + i32(2, N) + s(3, str(PV)))
pmt = Decimal(one_string(msg))

# Independent: PMT = -PV * r / (1 - (1+r)^-n), evaluated in float here so it
# cannot share an implementation with the engine.
r, pv = float(RATE), float(PV)
expected_pmt = -pv * r / (1.0 - (1.0 + r) ** (-N))
check("pmt 300k/6%/30y", abs(float(pmt) - expected_pmt) < 1e-6,
      f"{pmt} vs closed form {expected_pmt:.10f}")

# Identity: interest + principal for a period must equal the full payment.
_, mi = call("ComputeInterestPayment",
             s(1, str(RATE)) + i32(2, 1) + i32(3, N) + s(4, str(PV)))
_, mp = call("ComputePrincipalPayment",
             s(1, str(RATE)) + i32(2, 1) + i32(3, N) + s(4, str(PV)))
ipmt_v, ppmt_v = Decimal(one_string(mi)), Decimal(one_string(mp))
check("ipmt + ppmt == pmt", abs((ipmt_v + ppmt_v) - pmt) < Decimal("0.000000001"),
      f"{ipmt_v} + {ppmt_v} = {ipmt_v + ppmt_v} vs pmt {pmt}")

# Period-1 interest is definitionally balance x rate.
check("period-1 interest == PV*r", abs(abs(ipmt_v) - (PV * RATE)) < Decimal("0.000000001"),
      f"{abs(ipmt_v)} vs {PV * RATE}")

# --------------------------------------------------------------------------
# 2. Amortization -- internal consistency the schedule must satisfy.
# --------------------------------------------------------------------------
print("\nAmortization")

req = s(1, "300000") + s(2, "0.06") + i32(3, 360)
trailers, msg = call("ComputeAmortization", req)
rows, summary = [], None
for f, v in fields(msg):
    if f == 1:
        row = {}
        names = {1: "period", 2: "start", 3: "sched", 4: "extra", 5: "interest",
                 6: "principal", 7: "pmi", 8: "end"}
        for f2, v2 in fields(v):
            if f2 in names:
                row[names[f2]] = v2.decode() if isinstance(v2, bytes) else v2
        rows.append(row)
    elif f == 2:
        summary = {}
        snames = {1: "principal", 2: "interest", 3: "pmi", 4: "payments", 5: "months"}
        for f2, v2 in fields(v):
            if f2 in snames:
                summary[snames[f2]] = v2.decode() if isinstance(v2, bytes) else v2

check("schedule length", len(rows) == 360, f"{len(rows)} rows [{trailers}]")

if rows:
    # Every row: start - principal == end. A schedule that fails this is not a
    # schedule.
    worst = max(abs(Decimal(r["start"]) - Decimal(r["principal"]) - Decimal(r["end"]))
                for r in rows)
    check("row balance closes", worst < Decimal("0.000000001"),
          f"worst |start - principal - end| = {worst}")

    # The loan must actually retire.
    final = Decimal(rows[-1]["end"])
    check("final balance ~ 0", abs(final) < Decimal("0.01"), f"{final}")

    # Total principal repaid equals the amount borrowed.
    tot_p = Decimal(summary["principal"])
    check("total principal == loan", abs(tot_p - PV) < Decimal("0.01"),
          f"{tot_p} vs {PV}")
    print(f"        total interest over 30y: {Decimal(summary['interest']):.2f}")

# Overpayment must retire the loan EARLY -- the summary's own actual_term.
_, msg2 = call("ComputeAmortization",
               s(1, "300000") + s(2, "0.06") + i32(3, 360) + s(4, "500"))
early = None
for f, v in fields(msg2):
    if f == 2:
        for f2, v2 in fields(v):
            if f2 == 5:
                early = v2
check("overpayment shortens the term", early is not None and early < 360,
      f"actual_term_months = {early} (vs 360 scheduled)")

# --------------------------------------------------------------------------
# 3. Black-Scholes -- against put-call parity, which the engine cannot fake.
# --------------------------------------------------------------------------
print("\nBlack-Scholes")

S, K, R, VOL, T = 100.0, 100.0, 0.05, 0.2, 1.0
bs = dbl(1, S) + dbl(2, K) + dbl(3, R) + dbl(4, VOL) + dbl(5, T)
_, mc = call("PriceBlackScholes", bs)
_, mp2 = call("PriceBlackScholes", bs + i32(6, 1))
cvals = {f: v for f, v in fields(mc)}
pvals = {f: v for f, v in fields(mp2)}
c, p = cvals[1], pvals[1]

# C - P = S - K*exp(-rT). An identity, independent of the pricing formula.
parity_lhs = c - p
parity_rhs = S - K * math.exp(-R * T)
check("put-call parity", abs(parity_lhs - parity_rhs) < 1e-9,
      f"C-P = {parity_lhs:.10f} vs S-Ke^-rT = {parity_rhs:.10f}")

# Delta of a call minus delta of a put is exactly 1 for a non-dividend spot.
check("delta_call - delta_put == 1", abs((cvals[2] - pvals[2]) - 1.0) < 1e-9,
      f"{cvals[2]:.10f} - {pvals[2]:.10f} = {cvals[2] - pvals[2]:.10f}")

# Gamma and vega are identical for a call and a put at the same strike.
check("gamma call == gamma put", abs(cvals[3] - pvals[3]) < 1e-12,
      f"{cvals[3]:.12f} vs {pvals[3]:.12f}")
check("vega call == vega put", abs(cvals[5] - pvals[5]) < 1e-12,
      f"{cvals[5]:.12f} vs {pvals[5]:.12f}")

# --------------------------------------------------------------------------
# 4. Bonds -- price and yield must invert each other.
# --------------------------------------------------------------------------
print("\nBonds")

bond = dbl(1, 1000.0) + dbl(2, 0.05) + i32(3, 2) + dbl(4, 10.0) + dbl(5, 100.0)
_, mb = call("AnalyzeBond", bond + dbl(6, 0.04))
b = {f: v for f, v in fields(mb)}
price_at_4 = b[1]

# A bond whose coupon exceeds its yield trades above par.
check("5% coupon at 4% yield trades at a premium", price_at_4 > 1000.0,
      f"price {price_at_4:.6f} > par 1000")

# Feed that price back and the yield must come out where it went in.
_, mb2 = call("AnalyzeBond", bond + dbl(7, price_at_4))
b2 = {f: v for f, v in fields(mb2)}
check("price -> yield inverts", abs(b2[2] - 0.04) < 1e-8,
      f"recovered yield {b2[2]:.10f} vs 0.04")
check("duration < maturity", 0 < b[3] < 10.0, f"duration {b[3]:.6f} vs 10y maturity")
check("convexity positive", b[4] > 0, f"{b[4]:.6f}")

# --------------------------------------------------------------------------
# 5. Newest features -- HELOC, rental ROI, Fisher.
# --------------------------------------------------------------------------
print("\nHELOC / rental / Fisher (the features added upstream today)")

# 500k home, 300k owed, 80% LTV, 50k drawn at 7% over 15y monthly.
heloc = (s(1, "500000") + s(2, "300000") + s(3, "0.80") + s(4, "50000") +
         s(5, "0.07") + i32(6, 15) + i32(7, 12))
_, mh = call("ComputeHeloc", heloc)
hv = {f: v.decode() for f, v in fields(mh) if isinstance(v, bytes)}
# Equity ceiling is 500k*0.8 - 300k = 100k; 50k drawn leaves 50k.
check("HELOC available equity", abs(Decimal(hv[1]) - Decimal("50000")) < Decimal("0.01"),
      f"{Decimal(hv[1]):.2f} (500k*0.8 - 300k - 50k drawn)")
# Interest-only draw payment is 50000 * 0.07/12.
expect_draw = Decimal("50000") * Decimal("0.07") / Decimal(12)
check("HELOC draw payment is interest-only",
      abs(Decimal(hv[2]) - expect_draw) < Decimal("0.01"),
      f"{Decimal(hv[2]):.6f} vs {expect_draw:.6f}")
check("repayment payment exceeds draw payment",
      Decimal(hv[3]) > Decimal(hv[2]),
      f"{Decimal(hv[3]):.2f} amortizing > {Decimal(hv[2]):.2f} interest-only")

# Rental: 400k property, 100k cash in, 3000/mo rent, 800/mo opex, 1500/mo debt.
rental = (s(1, "400000") + s(2, "100000") + s(3, "3000") + s(4, "800") +
          s(5, "1500") + i32(6, 12))
_, mr = call("ComputeRentalRoi", rental)
rv = {f: v.decode() for f, v in fields(mr) if isinstance(v, bytes)}
# NOI = (3000-800)*12 = 26400, before debt service.
check("NOI excludes debt service",
      abs(Decimal(rv[1]) - Decimal("26400")) < Decimal("0.01"),
      f"{Decimal(rv[1]):.2f} = (3000-800)*12")
# Cash flow = NOI - 1500*12 = 26400 - 18000 = 8400.
check("cash flow is NOI minus debt",
      abs(Decimal(rv[2]) - Decimal("8400")) < Decimal("0.01"),
      f"{Decimal(rv[2]):.2f} = 26400 - 18000")
# Cap rate = NOI / value = 26400/400000 = 0.066.
check("cap rate = NOI / value",
      abs(Decimal(rv[4]) - Decimal("0.066")) < Decimal("0.000001"),
      f"{Decimal(rv[4]):.6f}")

# Fisher: exact form, (1+n) = (1+r)(1+i). Round trip must return the input.
_, mf = call("ComputeFisherRate", i32(1, 0) + dbl(2, 0.08) + dbl(3, 0.03))
real = one_double(mf)
expect_real = (1.08 / 1.03) - 1.0
check("Fisher real rate", abs(real - expect_real) < 1e-12,
      f"{real:.12f} vs (1.08/1.03)-1 = {expect_real:.12f}")
_, mf2 = call("ComputeFisherRate", i32(1, 1) + dbl(2, real) + dbl(3, 0.03))
check("Fisher round trip", abs(one_double(mf2) - 0.08) < 1e-12,
      f"{one_double(mf2):.12f} vs 0.08")

# --------------------------------------------------------------------------
# 6. Refusals -- malformed input must be rejected, not coerced.
# --------------------------------------------------------------------------
print("\nRefusals")

# BigDecimal's own parser skips non-digits, so "12x3" would silently become
# 123. The service validates before parsing; this proves it.
try:
    trailers, _ = call("ComputePayment", s(1, "12x3") + i32(2, 12) + s(3, "1000"))
    rejected = "grpc-status:0" not in trailers.replace(" ", "")
except Exception:
    rejected = True
check("malformed decimal refused", rejected, '"12x3" does not become 123')

# A compounding frequency the caller did not state is not invented.
try:
    trailers, _ = call("ComputeFutureValueDetailed",
                       s(1, "0.05") + i32(2, 10) + s(3, "1000"))
    rejected = "grpc-status:0" not in trailers.replace(" ", "")
except Exception:
    rejected = True
check("absent compound_frequency refused", rejected,
      "no default frequency is assumed")

# A bond with neither yield nor price cannot be answered.
try:
    trailers, _ = call("AnalyzeBond", dbl(1, 1000.0) + dbl(2, 0.05) + i32(3, 2) + dbl(4, 10.0))
    rejected = "grpc-status:0" not in trailers.replace(" ", "")
except Exception:
    rejected = True
check("bond with neither yield nor price refused", rejected,
      "no figure is derivable from the rest")

# A ragged batch is refused rather than truncated to the shortest column.
try:
    ragged = b""
    for v in (100000.0, 200000.0):
        ragged += tag(1, 1) + struct.pack("<d", v)
    ragged += tag(2, 1) + struct.pack("<d", 0.05)   # only ONE rate for two loans
    trailers, _ = call("ComputeAmortizationBatch", ragged)
    rejected = "grpc-status:0" not in trailers.replace(" ", "")
except Exception:
    rejected = True
check("ragged batch refused", rejected, "2 loans, 1 rate")

# --------------------------------------------------------------------------
# State assumptions -- the SHAPE of the payload, not the arithmetic.
#
# Over JSON rather than gRPC-Web, deliberately: the failure this section was
# written for is a wire-FORMAT one, and decoding it back through a protobuf
# reader would hide it. `refreshed_at` is a string field, so protobuf carries
# whatever bytes the database rendered and only a client parsing that string
# ever notices it is malformed.
#
# Postgres's to_char `OF` emits the SHORTEST offset -- "+00", not "+00:00" --
# which is not valid RFC3339 and is not what finance.proto documents. V8 parses
# it anyway and JavaScriptCore returns Invalid Date, so it works in the browser
# you test in and fails in Safari.
# --------------------------------------------------------------------------
print("\nState assumptions")

RFC3339_Z = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")

try:
    rq = urllib.request.Request(
        HOST + SVC + "GetStateAssumptions",
        data=b'{}',
        headers={"Content-Type": "application/json"},
    )
    states = json.loads(urllib.request.urlopen(rq, timeout=60).read()).get("states", [])
except Exception as exc:  # noqa: BLE001
    states = []
    check("GetStateAssumptions answers", False, str(exc))

if states:
    check("fifty states", len(states) == 50, f"{len(states)} rows")

    bad_ts = [r["slug"] for r in states
              if r.get("refreshedAt") and not RFC3339_Z.match(r["refreshedAt"])]
    check("refreshedAt is RFC3339 with a Z", not bad_ts,
          "all UTC-suffixed" if not bad_ts
          else f"{len(bad_ts)} malformed, e.g. {states[0].get('refreshedAt')}")

    # Every money field is a decimal STRING. A JSON number here would mean the
    # engine rounded to float64 before the client could, which is the whole
    # reason finance.proto states money as text.
    numeric = [r["slug"] for r in states
               if not isinstance(r.get("medianPrice"), str)
               or not isinstance(r.get("medianRent"), str)]
    check("money fields are strings", not numeric,
          "decimal strings" if not numeric else f"{len(numeric)} came back numeric")

    # Bounds are enforced in three places -- the C++ validator, the CHECK
    # constraints, and here -- because a value can only reach this response by
    # passing the first two, so a violation seen here means one of them is gone.
    out_of_band = [
        r["slug"] for r in states
        if not (Decimal("50000") <= Decimal(r["medianPrice"]) <= Decimal("3000000"))
        or not (Decimal("300") <= Decimal(r["medianRent"]) <= Decimal("8000"))
        or not (Decimal("0.05") <= Decimal(r["propertyTaxRate"]) <= Decimal("4"))
    ]
    check("every value is inside the plausibility bounds", not out_of_band,
          "50/50 in band" if not out_of_band else f"out of band: {out_of_band}")

    years = {r.get("dataYear") for r in states}
    check("one vintage across all fifty", len(years) == 1, f"data_year {years}")

    # The editorial columns must be absent-or-authored, never a figure the
    # refresh invented: the database grant makes them unwritable by the job, so
    # a value appearing here that nobody typed would mean that grant is gone.
    check("data_source names the ACS",
          all(r.get("dataSource", "").startswith("US Census ACS 5-year") for r in states),
          states[0].get("dataSource", ""))

# --------------------------------------------------------------------------

print()
if failures:
    for f in failures:
        print("FAIL:", f)
    print(f"\n{len(failures)} of {checks} checks failed")
    sys.exit(1)
print(f"VERDICT: {checks}/{checks} checks passed -- sensen.finance.Finance is live "
      f"and its answers satisfy independent identities")
