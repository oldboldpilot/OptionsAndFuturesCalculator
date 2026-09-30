#!/usr/bin/env python3
"""Summarise `[mortgage-assistant] timing:` lines from an engine log.

Prints median and aggregate decode tok/s (aggregate = sum tokens / sum decode
seconds, the method the 8-bit baseline of 15.42 tok/s used), median prefill ms
and median output tokens. Aggregate and median are both reported so a
divergence between them is visible rather than resolved in the flattering
direction.
"""
import re
import statistics as st
import sys

pat = re.compile(r"timing: prefill=([\d.]+)ms decode=([\d.]+)ms tokens=(\d+)")
pre, dec, tok = [], [], []
for line in open(sys.argv[1], errors="replace"):
    m = pat.search(line)
    if m:
        pre.append(float(m[1])); dec.append(float(m[2])); tok.append(int(m[3]))
per = [t / (d / 1000) for t, d in zip(tok, dec) if d > 0]
print(f"generations            : {len(tok)}")
print(f"aggregate decode tok/s : {sum(tok) / (sum(dec) / 1000):.2f}  ({sum(tok)} tok / {sum(dec)/1000:.0f} s)")
print(f"median decode tok/s    : {st.median(per):.2f}")
print(f"median prefill ms      : {st.median(pre):.1f}")
print(f"median output tokens   : {st.median(tok):.0f}")
