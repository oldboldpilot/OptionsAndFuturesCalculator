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
- [Closing-costs client notice](CLOSING_COSTS_CLIENT_NOTICE.md) — what each
  client must do for `ComputeClosingCosts`, answered per client because the
  answer differs: `frontend/` needs nothing (it calls one Finance RPC and this
  is not it), `clients/mortgagefv/` is re-vendored, and the RPC answers
  `UNIMPLEMENTED` until the backend carrying it ships.
- [State assumptions handoff](STATE_ASSUMPTIONS_HANDOFF.md) — the weekly US
  Census ACS refresh, now in the backend: `GetStateAssumptions` (open) and
  `RefreshStateAssumptions` (**partner only** — the single write on the finance
  service). Read §2 before parsing anything: every money field is a decimal
  string, and an empty `refreshed_at` means "never refreshed", NOT the epoch.
- [Business API](BUSINESS_API.md) — partner/commercial surface
- [API Security](API_SECURITY.md) — threat model, key handling, quota enforcement

## Architecture & operations

- [mortgagefvcalculator.com off Lovable](technical/MORTGAGEFV_RAILWAY_MIGRATION.md) —
  the whole migration onto Railway, including native gRPC over the private
  network (0.073s vs 1.97s), and the four defects found on the way: a transport
  default contradicting its own comment, a leading space in a Stripe key that
  broke every page, a database-wide search_path that broke GoTrue, and a webhook
  secret belonging to a different endpoint.
- [clang 22 → 23 toolchain migration](technical/TOOLCHAIN_CLANG23_MIGRATION.md) —
  the evaluation, the gates, and the three things that broke (two of them latent
  bugs of ours that a second compiler exposed). Also the overlay-install trap:
  libc++ 22's `ctype.h`/`inttypes.h`/`float.h`/`fenv.h` survive an overlay,
  shadow the C library's, and produce 700+ errors INSIDE libc++ 23.
- [llama.cpp under libc++ 23](technical/LLAMACPP_LIBCXX23.md) — how the vendored
  pin was made to compile on clang 23, and why it was PATCHED rather than bumped:
  llama.cpp is here to be an independent control for the parity probes, so moving
  the pin is changing the reference. Also carries the measured cost of compiling
  it at C++23 (module-scan steps 1 → 486) and the house rules deliberately NOT
  applied to upstream source.

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
| Frontend | Cloudflare **Workers static assets** (`frontend/wrangler.toml`) | `optionsandfuturescalculator.com` |
| Backend | Railway container (Envoy + C++23 engine) | `api.optionsandfuturescalculator.com` |
| Database | Railway PostgreSQL | `postgres.railway.internal:5432` |
| Billing | Cloudflare Worker `ofc-billing` | `ofc-billing.muyiwamc2.workers.dev` |

Two rows in that table were WRONG until 2026-09-09 and both wrongnesses cost
something. The frontend row said **Cloudflare Pages**: `wrangler pages deploy`
succeeds, prints a URL, and changes nothing a user can see, because the live
domains are Workers custom domains — five "completed" frontend deploys landed
nowhere. The billing row said **not yet deployed**; measured today,
`GET /health` on that Worker returns `200 {"ok":true,"supabaseConfigured":true}`,
so it is deployed and holds its Supabase credentials. What remains unproven is a
live Stripe checkout round trip, which is a different claim and is recorded as
such in CLAUDE.md.

Native gRPC does not survive the Railway **HTTP** ingress, and the reason is
narrower than this paragraph used to claim. It said "no request reaches the
container", which is false: the request reaches Envoy, reaches the engine, and
the engine returns the right answer — `x-envoy-upstream-service-time` proves it.
Railway's edge then strips the HTTP/2 **trailers**, and native gRPC carries
`grpc-status` only in a trailer, so the client reports `Stream removed`. gRPC-Web
frames its trailers into the body and is unaffected.

Native gRPC therefore DOES work over a path with no HTTP edge in it — the
Railway TCP proxy at `tokaido.proxy.rlwy.net:34513`, TLS, trailers intact, all
four services. See CLAUDE.md for the measured evidence and the `smoke_client`
invocation.

## Code reference

Generated 2026-09-09 by reading the sources, and verified afterwards: every
`path:line` citation in these documents was checked to resolve to a file that
exists at a line that exists. They describe what the code DOES; CLAUDE.md
describes why it is that way and what it cost to find out.

- [Feature inventory, as built](FEATURES.md) — what the system does today, per
  feature: the route or component, the RPC, the entitlement, and the gate that
  would fail if it regressed. Its last section is the useful one — features
  named in the planning documents that **do not exist in the code**, including
  an edge function that is checked in, has no caller, and writes to a table no
  migration creates.
- [gRPC surface reference](api/GRPC_SURFACE.md) — all four protos, every RPC and
  message, with the units, the decimal-string rule, the day-offset convention,
  and the JSON-transcoder path for each method.
- [Backend code map: the gRPC service layer](architecture/CODE_MAP_BACKEND_SERVICES.md)
  — `main.cpp`'s boot sequence and every RPC's delegate, gate and status codes.
- [Backend code map: grounding, grammar and pricing](architecture/CODE_MAP_BACKEND_VERIFICATION.md)
  — the five verification gates in execution order, and every static table with
  its size and the rule it encodes.
- [Backend code map: platform services](architecture/CODE_MAP_BACKEND_PLATFORM.md)
  — keys, quota, Postgres, the stores, the inference queues, market data, FIPS.
- [Frontend code map](architecture/CODE_MAP_FRONTEND.md) — routes, stores, the
  staleness token and its guards, components, and every assertion
  `check-export.mjs` makes against the emitted bytes.
- [Edge workers, vendored clients and database code map](architecture/CODE_MAP_EDGE_AND_CLIENTS.md)
  — the billing Worker, the vendored mortgagefv client, every migration, the
  deploy scripts and Envoy.
- [Model pipeline and operational scripts code map](architecture/CODE_MAP_ML_AND_SCRIPTS.md)
  — dataset generation, training, the evaluation harnesses and their
  denominators, the parity probes, and every script in `scripts/`.
- [Test and gate inventory](architecture/TEST_INVENTORY.md) — one row per target,
  with the column that matters: what breaks if it is deleted.

## Design specs

- [OPC parity design (2026-07-26)](superpowers/specs/2026-07-26-opc-parity-design.md)

## Session logs

- [2026-09-09 — the whole codebase documented, then the documentation verified: fabrication clusters in THIN files, an edge function wired to nothing, and an auto-correction that made the citations worse](session_logs/session_2026-09-09_code_documentation_sweep.md)
- [2026-09-05 — the finance surface swept end to end: two wrong answers served with 200 OK, three dead operations, and a probe that lied](session_logs/session_2026-09-05_finance_surface_sweep.md)
- [2026-08-20 — the mortgage assistant was never broken: three dataset defects, a vocabulary extension that starved the adapters, and three retrains to prove it](session_logs/session_2026-08-20_mortgage_corpus_and_three_retrains.md)
- [2026-08-28 (second) — the security question answered by measurement: TLS was observed but not enforced, RLS covered one table of three, and a FIPS gate that deliberately makes no FIPS claim](session_logs/session_2026-08-28_security_posture_and_batch_rpc.md)
- [2026-08-28 — rank 16 was the ceiling: +124 rows from adapter capacity, a holdout that scored memorised rows, and a struct that said BigDecimal while computing in double](session_logs/session_2026-08-28_rank64_v12_and_bigdecimal.md)
- [2026-08-27 — `ComputeRentVsBuy` refused 100% of assistant traffic with a green test suite: one caller's silence is the other caller's zero, and the dispatch was built against a model that no longer exists](session_logs/session_2026-08-27_rent_vs_buy_dispatch_and_v8_eval.md)
- [2026-08-25 — stale-branch sweep: the resident-memory work was serving production from an unmerged branch, and `ninja build_tests` never relinked the engine](session_logs/session_2026-08-25_stale_branch_sweep.md)
- [2026-08-19 — a closing-costs RPC the assistant could not name, the four tables an operation must reach, and a guarded use with an unguarded import](session_logs/session_2026-08-19_closing_costs_rpc.md)
- [2026-08-17 — the 404 was still serving ads on thirteen words: a denylist cannot protect the one route nobody enumerates, and the tests were written from the same list as the code](session_logs/session_2026-08-17_adsense_404_served_ads.md)
- [2026-08-16 (second) — mortgagefvcalculator's dark green, panels that were compressing rather than scrolling, and splitting the guides onto their own tab with the ads following the writing](session_logs/session_2026-08-16_theme_layout_and_guides_tab.md)
- [2026-08-16 — AdSense flagged ads on screens without publisher-content: 26 pages identical to the word, and a client-module import that shipped `if(undefined.some(...))`](session_logs/session_2026-08-16_adsense_publisher_content.md)
- [2026-08-13 (second) — the id skew's root cause was a prediction from a local counter, plus an intra-term lease fence and the M3b merge](session_logs/session_2026-08-13_id_skew_root_cause.md)
- [2026-08-13 — the SGEE queue promotion, its rollback on three defects, and the re-promotion under concurrent load](session_logs/session_2026-08-13_sgee_queue_promotion.md)
- [2026-08-12 (second) — a refused io_uring ring bricked the queue cluster for a day, four error layers hid it, and the await budget's bounds were discharged through Z3](session_logs/session_2026-08-12_queue_outage_io_uring_and_formal_bounds.md)
- [2026-08-12 — overlapping calculations let the last response win, quota refusals named the wrong tier, `/healthz` comment vs code, three stale documents](session_logs/session_2026-08-12_calculation_race_quota_label_and_stale_docs.md)
- [2026-08-11/12 — option-chain cache and freshness contract, SGEE Windows CI, queue-cluster incident, sensen merge, mortgagefvcalculator.com verified end to end](session_logs/session_2026-08-11_cache_cluster_windows_and_mortgage_verification.md)
- [2026-08-03 — frontend served by Workers not Pages, Pro gate verified, assistant eval corrected](session_logs/session_2026-08-03_frontend_workers_pro_gate_and_assistant_eval.md)
- [2026-08-01 — assistant, sensen performance, Pro tier](session_logs/session_2026-08-01_assistant_sensen_perf_and_pro.md)
- [2026-07-30 — libc++ std module investigation](session_logs/2026-07-30_libcxx_std_module_investigation.md)
- [2026-07-30 — calendar spread and matrix axis](session_logs/2026-07-30_calendar_spread_and_matrix_axis.md)
- [2026-07-29](session_logs/2026-07-29_session_log.md)
- [2026-07-26](session_logs/2026-07-26_session_log.md)
