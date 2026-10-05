#!/usr/bin/env python3
"""A CONFORMANT leaf, to the contract the gateway's own prober defines.

`gateway_tbqwf.cppm::curlProber` does GET {base_url}/v1/models and requires a
200 carrying a model list; anything else leaves the machine not-live, and a
machine that is not live is never a candidate, so `request.machine` stays empty
and the gate answers `no_permitted_placement`. That is the whole reason this
exists: it makes the liveness probe succeed so the admit path can be measured.
"""
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MODELS = ["qwen3_mortgage", "qwen3_strategy"]


class Leaf(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _send(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") == "/v1/models":
            self._send(200, {"object": "list",
                             "data": [{"id": m, "object": "model"} for m in MODELS]})
        elif self.path.rstrip("/") in ("/healthz", "/health"):
            self._send(200, {"status": "ok"})
        else:
            self._send(404, {"error": "not found"})

    def do_POST(self):
        n = int(self.headers.get("content-length") or 0)
        raw = self.rfile.read(n) if n else b"{}"
        try:
            req = json.loads(raw or b"{}")
        except Exception:
            req = {}
        if self.path.rstrip("/") == "/v1/chat/completions":
            self._send(200, {
                "id": "leaf-1", "object": "chat.completion", "model": req.get("model", ""),
                "choices": [{"index": 0, "finish_reason": "stop",
                             "message": {"role": "assistant",
                                         "content": "This is the sensen gateway CONFORMANCE LEAF, not an inference server. It exists to satisfy the prober contract so the admission path can be exercised. Point the machine row at a real leaf to serve real output."}}],
                "usage": {"prompt_tokens": 1, "completion_tokens": 7, "total_tokens": 8},
            })
        else:
            self._send(404, {"error": "not found"})

    def log_message(self, fmt, *a):
        sys.stderr.write("leaf " + (fmt % a) + "\n")


# Bind address is an argument so the container can bind 0.0.0.0 while a local
# run stays on loopback: a conformance leaf that listened on every interface by
# default would be an open endpoint on a developer's machine.
_host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
print(f"conformance leaf listening on {_host}:{sys.argv[1]}", flush=True)
ThreadingHTTPServer((_host, int(sys.argv[1])), Leaf).serve_forever()
