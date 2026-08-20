# `ComputeClosingCosts` — what each client has to do

@author Olumuyiwa Oluwasanmi

Status as of 2026-08-20, AFTER the deploy: `ComputeClosingCosts` is LIVE and
verified through the live ingress (all identity checks passed). This document
answers one question — *does the
frontend need to change to accommodate the new backend?* — separately for each
client, because the answer is different for each and a single yes/no would be
wrong for at least one of them.

## Short answer

| client | change needed | why |
| --- | --- | --- |
| `frontend/` (optionsandfuturescalculator.com) | **None.** | It calls exactly one Finance RPC and it is not this one. |
| `clients/mortgagefv/` (mortgagefvcalculator.com) | **Re-vendor + regenerate — already done**, commit `45b9df2`. | It is the consumer this RPC exists for. |
| Envoy / the proxy | **None.** | The route is a catch-all prefix and the transcoder uses `auto_mapping: true`. |

## `frontend/` needs nothing, and this is measured rather than assumed

Outside the generated stub, exactly ONE hand-written file in `frontend/src`
references a `sensen.finance.Finance` method:

```
src/store/useTreePricerStore.ts  ->  sensen.finance.Finance/PriceOptionTree
```

Every other match sits inside `src/grpc/FinanceServiceClientPb.ts`, which is
generated code — a method descriptor is not a call site. The only
`ClosingCosts` identifiers present are the PRE-EXISTING scalar fields
`RefinanceRequest.closing_costs` and `HomeNpvRequest.closing_costs_buy`, which
are exactly the structural gap that motivated a standalone RPC: closing costs
existed only as a lump sum inside two other operations, so there was no
itemisation for the assistant to name.

`frontend/src/grpc/finance_pb.*` is therefore STALE and that is harmless.
`gen-proto` is not part of `npm run build` (`build` is
`next build && node scripts/check-export.mjs`), so the committed stub is
regenerated only on demand — it cannot drift on its own and cannot fail a
build over an RPC nothing calls. Regenerate it if and when
optionsandfuturescalculator.com grows a closing-costs screen, not before: it
is a ~1300-line diff with no consumer today.

## `clients/mortgagefv/` — done, with two things the types cannot tell you

Re-vendored at `901ba0c` and regenerated with the pinned toolchain
(grpc-tools 1.13.1 / libprotoc 3.19.1, protoc-gen-grpc-web 1.5.0). The
regeneration is **purely additive**: 1530 lines added, zero deleted, two new
message classes, nothing removed. That is the property worth checking — codegen
exiting 0 would also be true of a run that silently dropped a message.

**`prepaid_interest_days` is the contract's only explicit-presence field.**
`clearPrepaidInterestDays()` and `setPrepaidInterestDays(0)` are DIFFERENT
requests: absent means the 15-day convention, an explicit 0 means zero days,
which a closing on the last day of a month genuinely owes. proto3 optional is
stable from protoc 3.15 and the pin is 3.19.1, so `has`/`clear` accessors
generate correctly.

**The percentages do not share a base.** `origination_fee_percent` and
`discount_points_percent` are shares of the LOAN; `title_settlement_percent`
and `transfer_tax_percent` are shares of the PRICE. Mixing them produces a
plausible figure rather than an error, which is why the engine recomputes every
line against its own base rather than checking a single sum.

**A credit reduces the TOTAL, not the itemisation.** Both
`itemised_subtotal` and `total_closing_costs` are returned. Folding a credit
into a line would stop the itemisation summing to its own subtotal — measured
against the live page, a 5,000 credit leaves the subtotal at 15,336 and moves
the headline to 10,336.

## How this is communicated, and the one thing that must be said

**`ComputeClosingCosts` was vendored ahead of its deploy on purpose** so the
client could be built and typed against it. It went LIVE on 2026-08-20, so the
`UNIMPLEMENTED` window is closed and the entries recording it have been
deleted rather than left standing as a hedge -- which is what this section
said the obligation was.

The transport table below is kept because it is a permanent property of the
ingress, not a fact about this one RPC: it is how you tell "the server does not
have this method" from "your headers are wrong" for ANY method.

**The status depends on the TRANSPORT, and only one of the two is the
contract.** Measured against production on 2026-08-20, before the deploy:

| transport | unmapped method | a mapped method, as control |
| --- | --- | --- |
| gRPC-Web (what this client uses) | `grpc-status: 12` | `grpc-status: 3`, "periods must be positive" |
| JSON via the transcoder (curl) | `grpc-status: 2`, **"Missing :te header"** | `{"value":"-3210.560578012665289866"}` |

12 is the real answer. The 2 is an artefact: `grpc_json_transcoder` cannot map
a method it has no descriptor for, so it passes the request through as raw
gRPC, which then fails on a header a JSON client never sends. **Anyone
reproducing this with curl is told to fix a header and chases the wrong thing
entirely.** Use a gRPC-Web request to see the real status.

**Every one of those four cells is HTTP 200** — as is a refusal, as is a quota
denial. HTTP status carries no information at this boundary; read
`grpc-status`. A promotion gate in this repository once asserted "all HTTP 200"
and passed while requests were being refused.

Liveness lives in TWO places and both were updated together on deploy:

1. `clients/mortgagefv/proto/finance.proto`'s header block, beside the pinned
   hashes — so liveness is updated when the hashes are, rather than in a file
   that can rot separately.
2. `clients/mortgagefv/README.md`'s failure-mode table and Known-constraints
   entry.

Both now say LIVE. This README has already carried a stale liveness claim in
the OTHER direction — it asserted that six deployed RPCs answered
`UNIMPLEMENTED` — and a wrong claim costs a caller real debugging in both
directions, which is why the "not yet deployed" entries were deleted on the day
rather than left standing.

## What was verified against production, and how

Deployed 2026-08-20. The cutover was confirmed by the boot sequence, not the
healthcheck: 6 `model is LOADED` lines (3 replicas x 2 assistants), 3 boot
banners (each replica booting ONCE, so not a crash loop), 0 `[ERROR]`, 0
`[WARN]`. Railway reports SUCCESS while a container crash-loops, so a green
deployment is not evidence anything is serving.

The RPC itself was then checked BY IDENTITY rather than against a recorded
figure — every itemised line recomputed against its OWN base, because a single
sum identity would catch a dropped term but never a swapped base:

| check | result |
| --- | --- |
| origination 405,000 x 0.0075 (LOAN base) | 3,037.50 |
| title 450,000 x 0.0055 (PRICE base) | 2,475.00 |
| transfer tax 450,000 x 0.005 (PRICE base) | 2,250.00 |
| prepaid interest 405,000 x r x 15/365 | 1,123.4589 |
| lines sum == `itemised_subtotal` | 15,335.9589 |
| `total_cash_to_close` == down payment + total | 60,335.9589 |
| 5,000 credit: subtotal unchanged, total 10,335.96 | pass |
| all-cash at exactly 1.0 (no loan, transfer tax still owed) | pass |
| explicit 0 prepaid days distinguishable from absent | pass |
| over-credit refused `FAILED_PRECONDITION` (9) | pass |

All checks passed. The subtotal and cash-to-close agree with the live
mortgagefvcalculator.com page, which is agreement between two independent
implementations rather than a figure copied forward.

## What the assistant does NOT yet do

`ComputeClosingCosts` is a `sensen.finance.Finance` RPC and works on its own.
The MORTGAGE ASSISTANT naming it from a plain-English utterance is a separate
capability with a separate gate, and deploying this RPC did not deliver it.

**Measured, so this is not a hedge.** The v3 candidate trained for it scores
**7/42 (16.7%)** numerically correct on the held-out closing-cost rows, with
**10/42** collapsing into a degenerate zero-loop at the final field, and it
regresses the other 26 operations by 39 rows at p = 3.5e-05. It was NOT
deployed; production serves v2, whose label space does not contain
`ComputeClosingCosts` at all, so the assistant's score for this operation in
production is **0/42 by construction**.

A client that needs "what are my closing costs on..." parsed from prose must
treat that as unavailable. A client that constructs the request itself is
unaffected and fully served — which is every case the type-safe generated
client covers.
