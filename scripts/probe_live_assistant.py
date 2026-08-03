#!/usr/bin/env python3
"""Verify the DEPLOYED strategy assistant over gRPC-Web.

@author Olumuyiwa Oluwasanmi

Native gRPC does not survive the Railway ingress -- smoke_client against the
custom domain fails with `Stream removed` and no request ever reaches the
container. Only gRPC-Web gets through, which is why this speaks the same
grpc-web-text framing scripts/probe_live_engine.py uses.

What this is actually testing, in order of how likely it is to be the thing
that broke:

  1. Whether the fine-tuned Qwen3-0.6B still emits usable output now that the
     KV cache is quantised to 8 bits by default. Every offline signal said it
     would -- a 40-step greedy rollout stayed token-identical to llama.cpp --
     but generation exercises the cache in a way the calculator RPCs never do,
     and no live parse had been run since the flip.

  2. Whether GP-ARA's verification stage is genuinely in the path, rejecting
     cross-field contradictions rather than being wired up but unreached.

  3. Whether the input-length caps and the Pro gate behave. PRO_GATE_MODE is
     `warn` in production, so an anonymous caller should still be served -- a
     refusal here would mean the gate is enforcing when it should be observing.

A Clarification is a legitimate outcome, not a failure: the model is trained to
ask rather than guess, and `ES` is deliberately ambiguous (E-mini S&P root and
Eversource Energy), so a clarification there is the CORRECT answer.

ALSO COVERS (added alongside GP-ARA constraint propagation for ambiguous-root
disambiguation, backend/src/modules/assistant_verification.cppm):

  4. GP-ARA constraint propagation: given an ambiguous root AND a
     Futures-category strategy the trader's own words did not decisively
     name as futures, the reasoner now settles it anyway by elimination
     through the SAME category-vs-asset_class rule
     `AssistantParamsDomain::translate()` already enforces everywhere else --
     see "GP-ARA CONSTRAINT PROPAGATION" below. It deliberately never
     attempts the mirror image (inferring EQUITY from an options-category
     strategy) -- that direction is the model's known "default when
     confused", and the ambiguous-ES case above (`long_put` from an
     utterance that never says "put") stays a Clarification for exactly that
     reason, unchanged.

  5. A general strategy-credibility gate, independent of any ambiguous root:
     `long_call`/`long_put` -- the two "bare direction" ids the fine-tune
     falls back to when confused -- are no longer trusted silently if the
     word that actually distinguishes them ("call"/"put") never appears in
     the request. Two live-observed defects motivated this: "Long ES, 30
     days, 1 contract." -> long_put (no "put" anywhere), and "Buy 100 shares
     of ES stock, Eversource, 30 days." -> long_call, quantity=100 (buying
     shares is not a long call, and this calculator prices no equity
     outright). Both now produce a Clarification instead of silently
     type-checking through. This DELIBERATELY changes the expected outcome
     of two cases below that previously resolved silently ("explicit equity
     wording" and "CL + equity") -- see their own comments.

IMPORTANT CAVEAT ON WHAT THIS SCRIPT CAN PROVE RIGHT NOW: cases 4 and 5 above
depend on backend code added in this change. This script talks to whatever is
CURRENTLY DEPLOYED behind HOST -- if that container has not been rebuilt and
redeployed since, every gate below that depends on the new reasoning will
report the OLD behaviour (this is expected, not a bug in the gate). The
mechanism itself is proven at the unit level instead, directly, in
backend/tests/test_assistant_verification.cpp
(`infer_ambiguous_root_asset_class`, `strategy_has_lexical_support`,
`is_unsupported_bare_direction_guess`) -- run those with
`ctest -R AssistantVerificationTest` after building. Re-run THIS script only
after `railway up`/redeploying to get a live signal on gates 4/5 specifically;
until then treat their PASS/FAIL here as informative, not authoritative.
"""

import base64
import json
import struct
import sys
import urllib.error
import urllib.request

HOST = sys.argv[1] if len(sys.argv) > 1 else "https://api.optionsandfuturescalculator.com"
SVC = "/calculator.assistant.StrategyAssistant/ParseStrategy"


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


def s(field, v):
    raw = v.encode()
    return tag(field, 2) + varint(len(raw)) + raw


def parse_varint(buf, i):
    n = 0
    shift = 0
    while True:
        b = buf[i]
        i += 1
        n |= (b & 0x7F) << shift
        if not b & 0x80:
            return n, i
        shift += 7


def decode(buf):
    """Flat protobuf walk -> {field_number: value}. Sub-messages stay bytes."""
    out = {}
    i = 0
    while i < len(buf):
        key, i = parse_varint(buf, i)
        field, wire = key >> 3, key & 7
        if wire == 0:
            out[field], i = parse_varint(buf, i)
        elif wire == 2:
            ln, i = parse_varint(buf, i)
            out[field] = buf[i:i + ln]
            i += ln
        elif wire == 5:
            out[field] = buf[i:i + 4]
            i += 4
        elif wire == 1:
            out[field] = buf[i:i + 8]
            i += 8
        else:
            break
    return out


def call(utterance, prior=""):
    req = s(1, utterance) + (s(2, prior) if prior else b"")
    frame = b"\x00" + struct.pack(">I", len(req)) + req
    r = urllib.request.Request(
        HOST + SVC,
        data=base64.b64encode(frame),
        headers={
            "Content-Type": "application/grpc-web-text",
            "Accept": "application/grpc-web-text",
            "X-Grpc-Web": "1",
        },
    )
    # gRPC can answer with a "trailers-only" response: grpc-status arrives in the
    # HTTP HEADERS with no body at all. Reading only the frames misses it and
    # reports an unhelpful "?" -- which is exactly how the oversized-input case
    # looked ambiguous when it was in fact being refused correctly.
    try:
        resp = urllib.request.urlopen(r, timeout=180)
        raw = resp.read()
        hdr_status = resp.headers.get("grpc-status")
        hdr_message = resp.headers.get("grpc-message")
    except urllib.error.HTTPError as e:
        return {"http_error": e.code, "body": e.read()[:200].decode(errors="replace")}

    if hdr_status is not None and not raw:
        return {"msg": None, "trailers": f"grpc-status:{hdr_status}",
                "hdr_status": hdr_status, "hdr_message": hdr_message or ""}

    # grpc-web-text base64-encodes each frame SEPARATELY and concatenates, so
    # decoding the whole blob at once is wrong on multi-frame responses.
    blob = b""
    for chunk in raw.split(b"="):
        if not chunk:
            continue
        pad = chunk + b"=" * (-len(chunk) % 4)
        try:
            blob += base64.b64decode(pad)
        except Exception:
            pass
    if not blob:
        blob = base64.b64decode(raw + b"=" * (-len(raw) % 4))

    msg, trailers = None, ""
    i = 0
    while i + 5 <= len(blob):
        flag = blob[i]
        ln = struct.unpack(">I", blob[i + 1:i + 5])[0]
        body = blob[i + 5:i + 5 + ln]
        i += 5 + ln
        if flag & 0x80:
            trailers = body.decode(errors="replace")
        else:
            msg = body
    return {"msg": msg, "trailers": trailers}


def describe(res):
    if "http_error" in res:
        return f"HTTP {res['http_error']}", res.get("body", "")
    status = "?"
    for part in res["trailers"].replace("\r\n", "\n").split("\n"):
        if part.startswith("grpc-status:"):
            status = part.split(":", 1)[1].strip()
    if res["msg"] is None:
        # gRPC status 3 is INVALID_ARGUMENT -- for the oversized case that is
        # the length cap doing its job, which is a PASS, not an unknown.
        names = {"0": "OK", "3": "INVALID_ARGUMENT", "7": "PERMISSION_DENIED",
                 "8": "RESOURCE_EXHAUSTED", "14": "UNAVAILABLE"}
        msg = res.get("hdr_message", "")
        return (f"STATUS {status} ({names.get(status, 'unnamed')})",
                msg or "(no message frame)")

    top = decode(res["msg"])
    if 1 in top:  # params
        p = decode(top[1])
        g = lambda k: p.get(k, b"").decode() if isinstance(p.get(k), bytes) else p.get(k, 0)
        return "PARAMS", (f"symbol={g(1)} asset_class={g(2)} strategy={g(3)} "
                          f"expiration_days={p.get(4,0)} quantity={p.get(5,0)}")
    if 2 in top:  # clarification
        c = decode(top[2])
        q = c.get(1, b"")
        return "CLARIFICATION", q.decode(errors="replace") if isinstance(q, bytes) else str(q)
    if 3 in top:  # refusal
        rf = decode(top[3])
        m = rf.get(2, b"")
        return "REFUSAL", (f"reason={rf.get(1,0)} " +
                           (m.decode(errors="replace") if isinstance(m, bytes) else ""))
    return f"grpc-status={status}", f"unrecognised outcome: {top}"


CASES = [
    ("a plain, in-distribution request",
     "Iron condor on SPY, 30 days out, one contract.", ""),
    ("a second in-distribution request, different strategy",
     "Buy a bull call spread on NVDA expiring in 45 days, 2 contracts.", ""),
    ("AMBIGUOUS ROOT -- ES is both the E-mini S&P root and Eversource Energy. "
     "Expect CLARIFICATION naming FUTURES first, options on the equity second "
     "-- a silent pick, or the old 'the NYSE utility stock' wording, is the "
     "bug this fixed",
     "Long ES, 30 days, 1 contract.", ""),
    ("ROUND TRIP, futures answer -- the same request with the clarification "
     "answered 'futures'. Expect PARAMS/FUTURES in ONE trip; the same "
     "question again would be a loop",
     "Long ES, 30 days, 1 contract.", "futures"),
    ("ROUND TRIP, options answer -- THE NEW CASE, and the one most likely to "
     "be broken: answering the reframed question with 'options' must "
     "resolve PARAMS/EQUITY in ONE trip, not loop back into the same "
     "question",
     "Long ES, 30 days, 1 contract.", "options"),
    ("OPTIONS ON A FUTURE, first turn -- names both futures and options in "
     "the same breath. covered_futures_call (an FOP) is category \"Futures\" "
     "in strategy_catalogue.cppm, so this must resolve PARAMS/FUTURES "
     "directly, never ask",
     "I want options on ES futures, 30 days out, 1 contract.", ""),
    ("explicit futures wording -- must NOT ask, the user already said it",
     "Buy an E-mini ES futures outright, 30 days, 1 contract.", ""),
    ("GP-ARA CONSTRAINT PROPAGATION -- the task brief's own example. A "
     "Futures-category strategy (futures_long) on the ambiguous ES root. "
     "NOTE: this exact phrase also contains the word \"futures\", which is "
     "independently decisive at the cheaper utterance-keyword step -- so a "
     "PASS here confirms no regression but does NOT by itself prove the NEW "
     "reasoning path fired; that is proven at the unit level instead "
     "(infer_ambiguous_root_asset_class in test_assistant_verification.cpp), "
     "where the utterance keyword is withheld on purpose. Expect PARAMS/"
     "FUTURES, no question, either way",
     "Long ES futures outright, 30 days", ""),
    ("explicit equity wording -- CHANGED by the new bare-direction-guess gate: "
     "the model reads this as long_call, quantity=100, but buying shares is "
     "not a long call and this calculator prices no equity outright, so this "
     "now expects CLARIFICATION rather than a silent PARAMS/EQUITY. Requires "
     "the redeployed backend; against the pre-existing deployment this will "
     "still show the OLD behaviour (PARAMS)",
     "Buy 100 shares of ES stock, Eversource, 30 days.", ""),
    ("CL + equity -- previously REFUSED outright as a contradiction (fixed "
     "earlier: a legitimate Colgate-Palmolive trade must not be refused for "
     "sharing a root with the CL futures contract). ALSO now expects "
     "CLARIFICATION rather than silent PARAMS, for the same bare-direction-"
     "guess reason as the case above (the model reads this as long_call too) "
     "-- requires the redeployed backend",
     "Buy 100 shares of Colgate-Palmolive stock, CL, 30 days.", ""),
    ("out-of-distribution commodity -- the model answers CND, which is not an "
     "instrument; verification must catch it",
     "3-2-1 crack spread on crude, 60 days.", ""),
    ("oversized input -- must be refused by the length cap, not truncated",
     "x" * 4000, ""),
]

print(f"target: {HOST}{SVC}\n")
results = []  # (label, kind, detail)
for label, utt, prior in CASES:
    kind, detail = describe(call(utt, prior))
    results.append((label, kind, detail))
    shown = utt if len(utt) <= 60 else f"{utt[:57]}... ({len(utt)} chars)"
    print(f"  {label}")
    print(f"    in  : {shown}")
    print(f"    out : {kind} | {detail}\n")

outcomes = [kind for _, kind, _ in results]
served = sum(1 for k in outcomes if k in ("PARAMS", "CLARIFICATION", "REFUSAL"))
print(f"served (params/clarification/refusal, i.e. the model ran and the "
      f"contract held): {served}/{len(CASES)}")
if outcomes and outcomes[0] == "PARAMS":
    print("VERDICT: assistant is LIVE and parsing on quantised KV")
else:
    print(f"VERDICT: first in-distribution case returned {outcomes[0] if outcomes else 'nothing'} "
          f"-- expected PARAMS; investigate before trusting the rest")


def field(detail, key):
    """Pulls `key=value` out of a describe() PARAMS detail string, or None."""
    for tok in detail.split():
        if tok.startswith(key + "="):
            return tok.split("=", 1)[1]
    return None


# --------------------------------------------------------------------------
# Strict pass/fail gates for the futures-first reframe and the futures-over-
# options precedence, keyed to the same CASES results above by label prefix
# so a change in CASES ordering cannot silently point this at the wrong row.
# --------------------------------------------------------------------------
print("\n=== strict gates: futures-first wording, and futures-over-options precedence ===")


def find_result(prefix):
    for label, kind, detail in results:
        if label.startswith(prefix):
            return kind, detail
    return None, None


gates_passed, gates_total = 0, 0


def gate(name, condition):
    global gates_passed, gates_total
    gates_total += 1
    if condition:
        gates_passed += 1
        print(f"  PASS: {name}")
    else:
        print(f"  FAIL: {name}")


kind, detail = find_result("AMBIGUOUS ROOT")
futures_pos = detail.find("futures") if detail else -1
options_pos = detail.find("options") if detail else -1
gate("ambiguous ES clarification names FUTURES before options",
     kind == "CLARIFICATION" and futures_pos != -1 and options_pos != -1 and
     futures_pos < options_pos)
gate("ambiguous ES clarification does not offer plain \"stock\" ownership",
     kind == "CLARIFICATION" and detail is not None and "stock" not in detail)

kind, detail = find_result("ROUND TRIP, futures answer")
gate("answering \"futures\" resolves PARAMS/FUTURES in one trip",
     kind == "PARAMS" and field(detail, "asset_class") == "FUTURES")

kind, detail = find_result("ROUND TRIP, options answer")
gate("answering \"options\" resolves PARAMS/EQUITY in one trip (the new case)",
     kind == "PARAMS" and field(detail, "asset_class") == "EQUITY")

kind, detail = find_result("OPTIONS ON A FUTURE")
gate("\"options on ES futures\" resolves PARAMS/FUTURES, not EQUITY, in one trip",
     kind == "PARAMS" and field(detail, "asset_class") == "FUTURES")

kind, detail = find_result("explicit futures wording")
gate("explicit E-mini wording still resolves PARAMS/FUTURES with no question",
     kind == "PARAMS" and field(detail, "asset_class") == "FUTURES")

# GP-ARA constraint propagation (task gate 1). See the CASES comment: this
# exact phrase also contains "futures", independently decisive at the
# keyword step, so this gate alone cannot distinguish "the old keyword step
# handled it" from "the new reasoning path handled it" -- it only proves no
# regression. The reasoning path itself is exercised without that shortcut in
# test_assistant_verification.cpp.
kind, detail = find_result("GP-ARA CONSTRAINT PROPAGATION")
gate("Futures-category strategy on ambiguous root resolves PARAMS/FUTURES, no question "
     "(regression check only -- see comment; the new-path-specific proof is unit-level)",
     kind == "PARAMS" and field(detail, "asset_class") == "FUTURES")

# CHANGED by the new bare-direction-guess gate (is_unsupported_bare_direction_guess):
# the model reads both of these as long_call/quantity=100, which is not a
# valid reading of "buy N shares" (this calculator prices no equity
# outright), so both now expect CLARIFICATION rather than a silent PARAMS.
# Requires the redeployed backend -- see the module docstring's caveat.
kind, detail = find_result("explicit equity wording")
gate("explicit shares wording now asks (long_call has no lexical support for a share purchase) "
     "-- requires redeploy, see docstring",
     kind == "CLARIFICATION")

kind, detail = find_result("CL + equity")
gate("CL + equity now asks for the same reason (long_call has no lexical support) "
     "-- requires redeploy, see docstring",
     kind == "CLARIFICATION")

kind, detail = find_result("out-of-distribution commodity")
gate("crack spread still refuses (not PARAMS)", kind != "PARAMS")

kind, detail = find_result("oversized input")
gate("oversized input still returns INVALID_ARGUMENT",
     isinstance(kind, str) and "INVALID_ARGUMENT" in kind)

print(f"\n=== strict gates: {gates_passed}/{gates_total} passed ===")
