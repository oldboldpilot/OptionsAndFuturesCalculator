#!/usr/bin/env python3
"""Extract the ordered sequence of `raw model output` blocks from an engine log.

Prints one sha256 per block and a final aggregate sha256 over the concatenation,
so two engine logs can be compared for BYTE IDENTITY of what the model emitted
rather than for an equal score. An equal score is compatible with rows moving in
both directions; identity is not.

The block body is length-delimited by the engine's own `(N bytes)`, so a payload
containing a newline is read correctly rather than truncated at the first one.
"""
import hashlib
import re
import sys

MARKER = re.compile(rb"\[mortgage-assistant\] raw model output \((\d+) bytes\): ")


def blocks(path: str) -> list[bytes]:
    data = open(path, "rb").read()
    out: list[bytes] = []
    for m in MARKER.finditer(data):
        n = int(m[1])
        out.append(data[m.end():m.end() + n])
    return out


def main() -> None:
    per_block = len(sys.argv) > 2 and sys.argv[2] == "--per-block"
    bs = blocks(sys.argv[1])
    agg = hashlib.sha256()
    for i, b in enumerate(bs):
        agg.update(b)
        if per_block:
            print(f"{i:4d} {hashlib.sha256(b).hexdigest()[:16]} {len(b):5d}")
    print(f"blocks {len(bs)}")
    print(f"aggregate sha256 {agg.hexdigest()}")


if __name__ == "__main__":
    main()
