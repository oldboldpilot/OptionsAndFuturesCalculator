#!/usr/bin/env python3
"""
Verify ComputeClosingCosts against the LIVE ingress, by identity.

@author Olumuyiwa Oluwasanmi

Envoy's grpc_json_transcoder covers every service with auto_mapping: true, so
each RPC is reachable as POST /<package>.<Service>/<Method> with a JSON body.
That is how this checks production without a browser -- native gRPC does not
survive the Railway ingress.

WHAT IT CHECKS, AND WHY IT IS NOT A RECORDED-FIGURE COMPARISON

Every line is recomputed HERE against its OWN base, because the percentages do
not share one: origination and discount points are shares of the LOAN, title
and transfer tax are shares of the PRICE. Mixing them yields a plausible figure
rather than an error, so a single sum identity would see a dropped term and
never a wrong base. Checking each line separately is what makes a swapped base
detectable.

The four cases after the arithmetic are the ones whose absence would each have
shipped a real defect:

  * a CREDIT reduces the TOTAL and leaves the itemisation alone -- folding it
    into a line would stop the itemisation summing to its own subtotal;
  * down_payment_percent == 1.0 is an ALL-CASH purchase, legal, with no loan
    but title/appraisal/recording/transfer/escrow still owed;
  * prepaid_interest_days ABSENT means the 15-day convention and an explicit 0
    means zero days -- a closing on the last day of a month owes zero, and a
    sentinel mapping 0 to 15 makes that unrepresentable;
  * a credit larger than the bill must be refused, and it is the deliberate
    FAILED_PRECONDITION: 100,000 is ordinary on a larger closing and wrong only
    relative to a subtotal that cannot be known until the itemisation runs.
"""
import json
import sys
import urllib.error
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "https://api.optionsandfuturescalculator.com"
URL = HOST.rstrip("/") + "/sensen.finance.Finance/ComputeClosingCosts"

BASE = {
    "home_price": "450000",
    "down_payment_percent": "0.10",
    "annual_rate": "0.0675",
    "origination_fee_percent": "0.0075",
    "discount_points_percent": "0",
    "other_lender_fees": "1400",
    "title_settlement_percent": "0.0055",
    "appraisal_fee": "650",
    "inspection_fee": "500",
    "recording_fees": "225",
    "transfer_tax_percent": "0.005",
    "homeowners_insurance_annual": "2100",
    "property_tax_annual": "6300",
    "tax_escrow_months": 3,
    "seller_lender_credits": "0",
    # prepaid_interest_days deliberately ABSENT -- the 15-day convention.
}

fails = []


def call(body, want_error=False):
    req = urllib.request.Request(
        URL, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        raw = e.read().decode(errors="replace")
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, {"_raw": raw[:400]}
    except Exception as e:                                    # noqa: BLE001
        return 0, {"_transport": str(e)[:200]}


def num(d, k):
    """Response money fields are decimal STRINGS; camelCase or snake_case."""
    if k in d:
        return float(d[k])
    cam = "".join(w if i == 0 else w.capitalize() for i, w in enumerate(k.split("_")))
    if cam in d:
        return float(d[cam])
    raise KeyError(f"{k} / {cam} not in response: {sorted(d)[:14]}")


def check(what, got, want, tol=0.005):
    ok = abs(got - want) <= tol
    print(f"  [{'PASS' if ok else 'FAIL'}] {what:44} got {got:14.4f}  want {want:14.4f}")
    if not ok:
        fails.append(what)


print(f"=== ComputeClosingCosts @ {URL} ===\n")

status, r = call(BASE)
print(f"HTTP {status}")
if status != 200:
    print(json.dumps(r, indent=2)[:900])
    sys.exit(1)

price = 450000.0
loan = price * (1 - 0.10)

print("\n-- every line recomputed against its OWN base --")
check("origination_fee   (LOAN x 0.0075)", num(r, "origination_fee"), loan * 0.0075)
check("discount_points   (LOAN x 0)", num(r, "discount_points"), loan * 0.0)
check("other_lender_fees (passthrough)", num(r, "other_lender_fees"), 1400.0)
check("title_settlement  (PRICE x 0.0055)", num(r, "title_settlement"), price * 0.0055)
check("appraisal_fee     (passthrough)", num(r, "appraisal_fee"), 650.0)
check("inspection_fee    (passthrough)", num(r, "inspection_fee"), 500.0)
check("recording_fees    (passthrough)", num(r, "recording_fees"), 225.0)
check("transfer_tax      (PRICE x 0.005)", num(r, "transfer_tax"), price * 0.005)
check("property_tax_escrow (6300 x 3/12)", num(r, "property_tax_escrow"), 6300.0 * 3 / 12)
check("prepaid_interest  (LOAN x r x 15/365)", num(r, "prepaid_interest"),
      loan * 0.0675 * 15 / 365)

lines = sum(num(r, k) for k in (
    "origination_fee", "discount_points", "other_lender_fees", "title_settlement",
    "appraisal_fee", "inspection_fee", "recording_fees", "transfer_tax",
    "homeowners_insurance_prepaid", "property_tax_escrow", "prepaid_interest"))
sub = num(r, "itemised_subtotal")
print("\n-- identities --")
check("lines sum == itemised_subtotal", lines, sub)
check("itemised_subtotal (recorded 15,335.96)", sub, 15335.9589, tol=0.01)
check("total_cash_to_close (recorded 60,335.96)", num(r, "total_cash_to_close"),
      60335.9589, tol=0.01)
check("cash_to_close == down_payment + total", num(r, "total_cash_to_close"),
      num(r, "down_payment") + num(r, "total_closing_costs"))

print("\n-- a credit reduces the TOTAL, not the itemisation --")
s2, r2 = call({**BASE, "seller_lender_credits": "5000"})
if s2 == 200:
    check("credited itemised_subtotal UNCHANGED", num(r2, "itemised_subtotal"), sub)
    check("credited total == subtotal - 5000", num(r2, "total_closing_costs"), sub - 5000.0)
else:
    fails.append("credit case"); print(f"  [FAIL] credit case HTTP {s2}: {str(r2)[:200]}")

print("\n-- all-cash: down_payment_percent == 1.0 is legal --")
s3, r3 = call({**BASE, "down_payment_percent": "1.0"})
if s3 == 200:
    check("all-cash origination_fee == 0", num(r3, "origination_fee"), 0.0)
    check("all-cash prepaid_interest == 0", num(r3, "prepaid_interest"), 0.0)
    check("all-cash transfer_tax still owed", num(r3, "transfer_tax"), price * 0.005)
else:
    fails.append("all-cash"); print(f"  [FAIL] all-cash refused HTTP {s3}: {str(r3)[:200]}")

print("\n-- explicit 0 days differs from ABSENT (15-day convention) --")
s4, r4 = call({**BASE, "prepaid_interest_days": 0})
if s4 == 200:
    check("explicit 0 days -> prepaid_interest == 0", num(r4, "prepaid_interest"), 0.0)
    same = abs(num(r4, "prepaid_interest") - num(r, "prepaid_interest")) < 0.005
    print(f"  [{'FAIL' if same else 'PASS'}] explicit 0 is DISTINGUISHABLE from absent")
    if same:
        fails.append("optional presence collapsed")
else:
    fails.append("zero-days"); print(f"  [FAIL] zero-days HTTP {s4}: {str(r4)[:200]}")

print("\n-- a credit larger than the bill is refused (FAILED_PRECONDITION) --")
s5, r5 = call({**BASE, "seller_lender_credits": "100000"})
code = r5.get("code")
ok = s5 != 200 and code == 9
print(f"  [{'PASS' if ok else 'FAIL'}] over-credit refused: HTTP {s5} code {code} "
      f"({str(r5.get('message'))[:70]})")
if not ok:
    fails.append("over-credit")

print("\n" + "=" * 62)
print(f"RESULT: {'ALL CHECKS PASSED' if not fails else f'{len(fails)} FAILED: {fails}'}")
sys.exit(1 if fails else 0)
