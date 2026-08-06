# Options & Futures Calculator Documentation Index

@author Olumuyiwa Oluwasanmi

## Core

- [Root CLAUDE Guide](../CLAUDE.md) — architecture, gRPC surface, deployment, DNS
- [Product Requirements Document](PRD_OPTIONS_AND_FUTURES_CALCULATOR.md)

## API

- [Finance API](FINANCE_API.md) — the `sensen.finance.Finance` service: ~50 functions
  across time value of money, mortgages, bonds, futures, options and portfolio
  statistics. Note the `QUOTA_POLICY` example in this document omits the `pro`
  tier; an unknown tier silently falls back to the *anonymous* allowance, so a
  policy copied from here verbatim would under-serve paying subscribers.
- [Business API](BUSINESS_API.md) — partner/commercial surface
- [API Security](API_SECURITY.md) — threat model, key handling, quota enforcement

## Architecture & operations

- [Tensor observer](../backend/docs/TENSOR_OBSERVER.md) — runtime per-layer
  inspection of the inference engine, attachable by environment variable to an
  already-built release binary
- [Evaluating a strategy-assistant model](guides/ASSISTANT_EVALUATION.md) — how the
  assistant is served in-process by sensen, and how to measure a candidate against
  it. Do not use llama.cpp: it scored the deployed model 7/16 and triggered a
  retrain for a regression that did not exist (13/16 measured correctly).
- [Strategy assistant pipeline](STRATEGY_ASSISTANT_PIPELINE.md) — dataset
  generation through QLoRA, merge/export/quantize, and the serving constraints
- [Mortgage assistant pipeline](MORTGAGE_ASSISTANT_PIPELINE.md) — the SECOND
  assistant end to end: proto-derived dataset, QLoRA, conversion, the four
  serving constraints, the GP-ARA verification gate, and its honest 27.8%
  measured accuracy. Do not quote `evaluate.py`'s 31.7%/36.4% — it measures the
  bf16 intermediate, which is not what ships.
- [Mortgage assistant model distribution](MORTGAGE_MODEL_DISTRIBUTION.md) — how the
  SECOND fine-tuned Qwen3-0.6B is checksummed, pinned and reaches the image.
  Distribution only; the strategy pipeline document is about a different model and
  says so. Neither GGUF can travel through `railway up`. **Its hosting half is
  superseded** — the private HF repo was deleted 2026-08-05 and a replacement is
  being designed; read the status banner first.
- [Billing worker](../workers/billing/README.md) — Stripe checkout, licence
  minting, Supabase tier writes. **Register all four Stripe webhook events**; the
  README currently lists three, and omitting
  `customer.subscription.deleted` means Stripe never delivers it, so a cancelled
  subscriber keeps Pro indefinitely.

**Infrastructure**

| Layer | Host | Domain |
| --- | --- | --- |
| Frontend | Cloudflare Pages | `optionsandfuturescalculator.com` |
| Backend | Railway container (Envoy + C++23 engine) | `api.optionsandfuturescalculator.com` |
| Database | Railway PostgreSQL | `postgres.railway.internal:5432` |
| Billing | Cloudflare Worker | not yet deployed |

Native gRPC does not currently survive the Railway ingress — `smoke_client`
against `api.optionsandfuturescalculator.com:443` fails with `Stream removed`
and no request reaches the container. Only gRPC-Web works from outside. Verify
production behaviour through the browser path or `railway logs`, not the native
smoke client.

## Design specs

- [OPC parity design (2026-07-26)](superpowers/specs/2026-07-26-opc-parity-design.md)

## Session logs

- [2026-08-03 — frontend served by Workers not Pages, Pro gate verified, assistant eval corrected](session_logs/session_2026-08-03_frontend_workers_pro_gate_and_assistant_eval.md)
- [2026-08-01 — assistant, sensen performance, Pro tier](session_logs/session_2026-08-01_assistant_sensen_perf_and_pro.md)
- [2026-07-30 — libc++ std module investigation](session_logs/2026-07-30_libcxx_std_module_investigation.md)
- [2026-07-30 — calendar spread and matrix axis](session_logs/2026-07-30_calendar_spread_and_matrix_axis.md)
- [2026-07-29](session_logs/2026-07-29_session_log.md)
- [2026-07-26](session_logs/2026-07-26_session_log.md)
