#!/usr/bin/env python3
"""EncoderQueueProbeTest: the latency probe's `answered` count.

The bench reads arm L's in-process count from it (INFERENCE_QUEUE=local logs no per-request line), so
it must count the requests that were ANSWERED -- warm-up included, a request that errored left out --
not the requests sent. The gRPC client is replaced by a scripted one: one warm-up call and one
measured call fail, and the rest succeed. Exits 77 (skipped) when grpc is not installed.
"""
import contextlib
import importlib.util
import io
import json
import os
import sys
import types

PROBE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'scripts', 'encoder_queue_probe.py')

try:
    import grpc  # noqa: F401
except ImportError:
    print('grpc not installed: skipped')
    sys.exit(77)

spec = importlib.util.spec_from_file_location('encoder_queue_probe', PROBE)
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

FAILING_CALLS = (2, 5)  # 1-based: the 2nd call is a warm-up, the 5th is a measured request
WARMUP, REQUESTS = 3, 6
calls = {'n': 0}


def scripted_call(stub_call, pb, call):
    calls['n'] += 1
    error = 'UNAVAILABLE: scripted' if calls['n'] in FAILING_CALLS else None
    return 0.001, None, error, None


probe.one_call = scripted_call
probe.load_stubs = lambda: (None, lambda channel: types.SimpleNamespace(Op=None), 'Op')
probe.load_requests = lambda limit=None: [(i, [{'text': str(i)}]) for i in range(WARMUP)]
probe.make_channel = lambda args: None

args = types.SimpleNamespace(concurrency=1, requests=REQUESTS, warmup=WARMUP, target='scripted')
out = io.StringIO()
with contextlib.redirect_stdout(out):
    status = probe.cmd_latency(args)
cell = json.loads(out.getvalue())

expected = (WARMUP - 1) + (REQUESTS - 1)
failures = []
if status != 1:
    failures.append(f'an errored cell must exit 1, got {status}')
if cell.get('errors') != 1:
    failures.append(f"errors: expected 1, got {cell.get('errors')}")
if cell.get('answered') != expected:
    failures.append(f"answered: expected {expected} (errors excluded, warm-up included), got {cell.get('answered')}")
for failure in failures:
    print('FAIL:', failure)
print('0 failures' if not failures else f'{len(failures)} failures')
sys.exit(1 if failures else 0)
