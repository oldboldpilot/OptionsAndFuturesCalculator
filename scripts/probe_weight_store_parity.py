#!/usr/bin/env python3
"""Send the same real utterances to two engines and require identical answers.

The comparison is over the CANONICAL form of each ParseResponse (fields sorted at
every level), not its raw bytes: the params are a protobuf map, whose iteration
order differs between processes, so identical answers have different bytes.

    python3 scripts/probe_weight_store_parity.py --a localhost:50971 --b localhost:50972 \
        --val agent/dataset/data_mortgage/val.jsonl --n 12

For comparing a DENSE engine against one started with MORTGAGE_WEIGHT_STORE=llq
(both on their own ports, both with MORTGAGE_MODEL_PATH at the same GGUF). It
speaks native gRPC with a generic channel and hand-encoded protobuf, like
probe_key_and_quota.py, and compares the canonical ParseResponse: a parse that
"looks the same" but differs in one digit of one money field is exactly the
failure this comparison is for. `--self-test` proves that in both directions
(a permuted map is equal, a one-digit change is not) without any engine.

Exits 0 only if every response is byte-identical. It prints one line per
utterance and the aggregate SHA-256 of each side, and it refuses to run unless
each port is held by exactly one process (engines bind with SO_REUSEPORT, so a
stale engine on the same port would answer half the requests with another
model and this script would report a difference that is not the code's).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys

import grpc


def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _read_varint(b: bytes, i: int):
    n = shift = 0
    while True:
        if i >= len(b):
            raise ValueError("truncated varint")
        c = b[i]
        i += 1
        n |= (c & 0x7F) << shift
        if not c & 0x80:
            return n, i
        shift += 7


def canonical(b: bytes) -> bytes:
    """The message with its fields in a canonical order, recursing into any
    length-delimited field that itself parses as a message.

    `FinanceParams.params` is a protobuf `map<string,string>`, and a map has no
    defined iteration order: the engine emits the same entries in hash-bucket
    order that varies between PROCESSES. A byte-for-byte comparison of two
    engines therefore reports a difference on identical answers (CLAUDE.md,
    "A BYTE-IDENTITY GATE IS INVALID ON THIS RESPONSE"). Comparing the parsed
    content -- here, the fields sorted at every level -- is the valid gate. The
    same function runs on both sides, so two answers are equal under it exactly
    when they hold the same fields with the same values, in any order.
    """
    fields = []
    i = 0
    try:
        while i < len(b):
            key, i = _read_varint(b, i)
            num, wire = key >> 3, key & 7
            if num == 0:
                raise ValueError("field 0")
            if wire == 0:
                v, i = _read_varint(b, i)
                val = b"v" + v.to_bytes(10, "little")
            elif wire == 1:
                val = b"d" + b[i:i + 8]; i += 8
            elif wire == 5:
                val = b"f" + b[i:i + 4]; i += 4
            elif wire == 2:
                ln, i = _read_varint(b, i)
                if i + ln > len(b):
                    raise ValueError("truncated")
                inner = b[i:i + ln]; i += ln
                try:
                    val = b"m" + canonical(inner)
                except ValueError:
                    val = b"s" + inner
            else:
                raise ValueError("group/unknown wire type")
            fields.append((num, val))
    except (ValueError, IndexError):
        raise ValueError("not a message")
    out = bytearray()
    for num, val in sorted(fields):
        out += _varint(num) + _varint(len(val)) + val
    return bytes(out)


def parse_request(utterance: str) -> bytes:
    raw = utterance.encode()
    return b"\x0a" + _varint(len(raw)) + raw  # field 1, length-delimited


def holders(port: int) -> int:
    out = subprocess.run(["ss", "-ltnpH", f"sport = :{port}"], capture_output=True, text=True).stdout
    pids = {p.split(",")[0] for line in out.splitlines() for p in line.split("pid=")[1:]}
    return len(pids)


def utterances(path: str, n: int) -> list[str]:
    out = []
    for line in open(path):
        conv = json.loads(line)["conversations"]
        users = [m for m in conv if m["role"] == "user"]
        if len(users) == 1:
            out.append(users[0]["content"])
        if len(out) == n:
            break
    return out


def self_test() -> int:
    def entry(k: str, v: str) -> bytes:
        kv = b"\x0a" + _varint(len(k)) + k.encode() + b"\x12" + _varint(len(v)) + v.encode()
        return b"\x12" + _varint(len(kv)) + kv

    def params(entries: list[bytes]) -> bytes:
        body = b"\x0a\x0eComputePayment" + b"".join(entries)
        return b"\x0a" + _varint(len(body)) + body

    e = [entry("rate", "0.006858"), entry("periods", "360"), entry("present_value", "363400.00")]
    base = canonical(params(e))
    ok = True
    ok &= canonical(params(list(reversed(e)))) == base
    print(f"  {'PASS' if ok else 'FAIL'}: the same map in another order is EQUAL")
    changed = canonical(params([entry("rate", "0.006859"), e[1], e[2]]))
    d = changed != base
    print(f"  {'PASS' if d else 'FAIL'}: one digit changed in one value is DIFFERENT")
    renamed = canonical(params([entry("rates", "0.006858"), e[1], e[2]]))
    r = renamed != base
    print(f"  {'PASS' if r else 'FAIL'}: one key renamed is DIFFERENT")
    return 0 if (ok and d and r) else 1


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--val", default="agent/dataset/data_mortgage/val.jsonl")
    ap.add_argument("--n", type=int, default=12)
    ap.add_argument("--timeout", type=float, default=120.0)
    a = ap.parse_args()

    for addr in (a.a, a.b):
        port = int(addr.rsplit(":", 1)[1])
        h = holders(port)
        if h != 1:
            print(f"FATAL: {h} processes hold :{port}; need exactly 1", file=sys.stderr)
            return 2

    method = "/mortgage.assistant.MortgageAssistant/ParseOperation"
    chans = {k: grpc.insecure_channel(v) for k, v in (("a", a.a), ("b", a.b))}
    calls = {k: c.unary_unary(method, request_serializer=lambda x: x,
                              response_deserializer=lambda x: x) for k, c in chans.items()}
    sha = {"a": hashlib.sha256(), "b": hashlib.sha256()}  # over CANONICAL bytes
    differ = 0
    kinds = {}
    for i, u in enumerate(utterances(a.val, a.n)):
        resp = {}
        for k in ("a", "b"):
            try:
                resp[k] = calls[k](parse_request(u), timeout=a.timeout)
            except grpc.RpcError as e:
                resp[k] = f"RPC {e.code()} {e.details()}".encode()
            try:
                cb = canonical(resp[k])
            except ValueError:
                cb = resp[k]
            sha[k].update(len(cb).to_bytes(4, "little") + cb)
        try:
            same = canonical(resp["a"]) == canonical(resp["b"])
        except ValueError:
            same = resp["a"] == resp["b"]
        differ += 0 if same else 1
        kind = "refusal" if resp["a"].startswith(b"RPC") else f"{len(resp['a'])} B"
        kinds[kind] = kinds.get(kind, 0) + 1
        print(f"{i:>3} {'IDENTICAL' if same else 'DIFFERS  '} {len(resp['a']):>4} B vs {len(resp['b']):>4} B  {u[:70]!r}")
        if not same:
            print(f"      a: {resp['a'][:160]!r}\n      b: {resp['b'][:160]!r}")
    print(f"\na sha256 {sha['a'].hexdigest()}\nb sha256 {sha['b'].hexdigest()}")
    print(f"{differ} of {i + 1} responses differ")
    return 0 if differ == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
