#!/usr/bin/env python3
"""Drive the encoder assistants over native gRPC: measure latency, or dump canonical answers.

@author Olumuyiwa Oluwasanmi

    encoder_queue_probe.py latency --target 127.0.0.1:50871 --surface mortgage -c 8 -n 240
    encoder_queue_probe.py dump    --target 127.0.0.1:50871 --surface strategy --out answers.jsonl

WHY THIS EXISTS. The encoder assistants now travel through the shared queue (INFERENCE_QUEUE=sgee)
when one is configured. Two claims need an instrument that is the same on both sides of the
comparison:

  * `latency` -- what the queue ADDS. N requests at concurrency C, wall time per request, and the
    p50/p95/max. Rows cycle through the corpus so the cache of one utterance does not flatter it.
  * `dump` -- the served answer for every corpus row, canonicalised (a protobuf map has no defined
    iteration order, so two engines' bytes differ for reasons that are not the answer). Two dumps
    are equal iff `diff` says so; that is the "same answer from the queue as from the process"
    claim, over the whole RPC and not only the chain.

THE CORPORA are the repository's own: the 272-row mortgage visitor regression
(backend/tests/data/visitor_regression.jsonl, two-turn rows replayed as turn one then the visitor's
reply with the question echoed, per the engine's contract) and the 1500-row strategy holdout
(agent/dataset/data/val.jsonl, a second user turn sent as prior_clarification).
"""
import argparse
import concurrent.futures as cf
import json
import os
import statistics
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'agent', 'train'))

import grpc  # noqa: E402
from google.protobuf import json_format  # noqa: E402


def load_stubs(surface):
    if surface == 'mortgage':
        import mortgage_assistant_pb2 as pb, mortgage_assistant_pb2_grpc as pbg  # noqa: E401
        return pb, pbg.MortgageAssistantStub, 'ParseOperation'
    import assistant_pb2 as pb, assistant_pb2_grpc as pbg  # noqa: E401
    return pb, pbg.StrategyAssistantStub, 'ParseStrategy'


def load_requests(surface, limit=None):
    """[(id, [call, ...])] where a call is a dict of ParseRequest fields; later calls may name
    `echo_question` (take the previous response's question)."""
    out = []
    if surface == 'mortgage':
        path = os.path.join(ROOT, 'backend', 'tests', 'data', 'visitor_regression.jsonl')
        for line in open(path):
            row = json.loads(line)
            calls = [{'utterance': row['utterance']}]
            for turn in row.get('turns', [])[1:]:
                calls.append({'utterance': row['utterance'], 'prior_clarification': turn['reply'],
                              'echo_question': True})
            out.append((row['id'], calls))
    else:
        path = os.path.join(ROOT, 'agent', 'dataset', 'data', 'val.jsonl')
        for i, line in enumerate(open(path)):
            convo = json.loads(line)['conversations']
            users = [m['content'] for m in convo if m['role'] == 'user']
            calls = [{'utterance': users[0]}]
            if len(users) > 1:
                calls.append({'utterance': users[0], 'prior_clarification': users[1]})
            out.append((f'strategy-{i:04d}', calls))
    return out[:limit] if limit else out


def make_channel(args):
    return grpc.insecure_channel(args.target)


def one_call(stub_call, pb, call, last_question):
    fields = {k: v for k, v in call.items() if k != 'echo_question'}
    if call.get('echo_question') and last_question:
        fields['prior_question'] = last_question
    request = pb.ParseRequest(**fields)
    started = time.perf_counter()
    try:
        response = stub_call(request, timeout=30)
        error = None
    except grpc.RpcError as e:  # a refusal travels as OK; this is the transport/engine failing
        response, error = None, f'{e.code().name}: {e.details()}'
    return time.perf_counter() - started, response, error, request


def question_of(response):
    if response is None:
        return ''
    which = response.WhichOneof('outcome')
    return getattr(response, which).question if which == 'clarification' else ''


def cmd_latency(args):
    pb, stub_cls, method = load_stubs(args.surface)
    reqs = load_requests(args.surface)
    # Single-call rows only: a latency sample should be one request, not a conversation.
    singles = [calls[0] for _, calls in reqs]
    stub_call = getattr(stub_cls(make_channel(args)), method)
    for call in singles[:args.warmup]:
        one_call(stub_call, pb, call, '')
    lat, errors = [], 0
    started = time.perf_counter()
    with cf.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        futures = [pool.submit(one_call, stub_call, pb, singles[i % len(singles)], '')
                   for i in range(args.requests)]
        for f in futures:
            dt, _, error, _ = f.result()
            if error:
                errors += 1
            else:
                lat.append(dt * 1000.0)
    wall = time.perf_counter() - started
    lat.sort()
    pick = lambda q: lat[min(len(lat) - 1, int(q * len(lat)))] if lat else float('nan')
    print(json.dumps({'target': args.target, 'surface': args.surface, 'concurrency': args.concurrency,
                      'requests': args.requests, 'errors': errors, 'p50_ms': round(pick(0.50), 2),
                      'p95_ms': round(pick(0.95), 2), 'max_ms': round(lat[-1], 2) if lat else None,
                      'mean_ms': round(statistics.fmean(lat), 2) if lat else None,
                      'throughput_rps': round(args.requests / wall, 1)}))
    return 1 if errors else 0


def cmd_dump(args):
    pb, stub_cls, method = load_stubs(args.surface)
    reqs = load_requests(args.surface, args.limit)
    stub_call = getattr(stub_cls(make_channel(args)), method)

    def run_row(item):
        row_id, calls = item
        records, question = [], ''
        for call in calls:
            _, response, error, request = one_call(stub_call, pb, call, question)
            body = (json_format.MessageToDict(response, preserving_proto_field_name=True)
                    if response is not None else {'transport_error': error})
            records.append({'request': json_format.MessageToDict(request, preserving_proto_field_name=True),
                            'response': body})
            question = question_of(response)
        return {'id': row_id, 'calls': records}

    errors = 0
    with cf.ThreadPoolExecutor(max_workers=args.concurrency) as pool, open(args.out, 'w') as sink:
        for rec in pool.map(run_row, reqs):
            errors += sum('transport_error' in c['response'] for c in rec['calls'])
            sink.write(json.dumps(rec, sort_keys=True) + '\n')
    print(f'dumped {len(reqs)} rows to {args.out}; transport errors: {errors}')
    return 1 if errors else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('latency', 'dump'):
        p = sub.add_parser(name)
        p.add_argument('--target', required=True)
        p.add_argument('--surface', choices=('mortgage', 'strategy'), required=True)
        p.add_argument('-c', '--concurrency', type=int, default=1)
        if name == 'latency':
            p.add_argument('-n', '--requests', type=int, default=200)
            p.add_argument('--warmup', type=int, default=10)
        else:
            p.add_argument('--out', required=True)
            p.add_argument('--limit', type=int)
    args = ap.parse_args()
    return {'latency': cmd_latency, 'dump': cmd_dump}[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
