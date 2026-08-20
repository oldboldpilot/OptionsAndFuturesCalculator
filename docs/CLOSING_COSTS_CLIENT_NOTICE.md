# `ComputeClosingCosts` — what each client has to do

@author Olumuyiwa Oluwasanmi

Status as of 2026-08-20. This document answers one question — *does the
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

**`ComputeClosingCosts` is vendored ahead of its deploy on purpose** so the
client can be built and typed against it. Until the backend carrying it ships,
calling it answers `UNIMPLEMENTED` (grpc-status 12).

That fact is recorded in TWO places and both are load-bearing:

1. `clients/mortgagefv/proto/finance.proto`'s header block, beside the pinned
   hashes — so liveness is updated when the hashes are, rather than in a file
   that can rot separately.
2. `clients/mortgagefv/README.md`'s failure-mode table, whose `err.code === 12`
   row previously read *"Not currently true of any RPC in this contract"*.

That row is why this section exists. This README has already carried a stale
liveness claim in the OTHER direction — it asserted that six deployed RPCs
answered `UNIMPLEMENTED` — and a wrong liveness claim costs a caller real
debugging in both directions. Delete the entry when the RPC goes live; do not
leave it standing as a hedge.

## What the assistant does NOT yet do

`ComputeClosingCosts` is a `sensen.finance.Finance` RPC and works on its own.
The MORTGAGE ASSISTANT naming it from a plain-English utterance is a separate
capability with a separate gate, and it is not covered by deploying this RPC.
A client that needs "compute my closing costs" parsed from prose should treat
that as unavailable until the assistant's own holdout says otherwise; a client
that constructs the request itself is unaffected.
