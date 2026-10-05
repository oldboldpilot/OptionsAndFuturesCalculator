# Seeding the sensen gateway, and the four things that are easy to get wrong

Every one of these cost a measured `403 no_permitted_placement` before it was
found, and none of them is guessable from the seed's field names.

## 1. `reachable_from` is a list of GATEWAY IDs, not networks

`gateway_peer.cppm::classify` marks a leaf **direct** only when its
`reachable_from` contains the gateway's own `self` id, and
`gateway_main.cpp` reads that from `SENSEN_GATEWAY_ID`, defaulting to
**`sensen-gateway`**. A machine that is not direct (and not reachable via a
peer) is never `track`ed, so the probe loop never probes it, so it is never
live, so it is never a candidate — and `GateTable::decide` then tests
`permitted(principal, "")` against an EMPTY machine and answers
`no_permitted_placement`.

The machine row also has a `network` column (`wan`/`lan`/`vpn`), which is what
makes `reachable_from` look like it should hold networks. It must not. Seeding
`["wan","vpn","lan"]` yields a gateway that refuses every request while every
row in the store looks correct.

## 2. Identifiers must be valid unquoted Prolog atoms

Lowercase, alphanumeric, underscore. `renderWorldFacts` does quote unsafe
atoms, so a dotted model id like `qwen3-0.6b-mortgage` is handled — but the
quoting exists precisely because a dot or a hyphen would otherwise end the atom
early, so keeping ids `[a-z0-9_]` removes the question.

## 3. A budget is required, and `0` budgets is not "unlimited"

With no budget row covering the principal (directly or through a group) the
decision is `escalate / budget_unmeasured`, HTTP 503 — not an admit. The window
must span now: `window_start <= now <= window_end`.

## 4. `max_tokens` is REQUIRED on every completion

The admission ticket bounds the leaf's work by it, so a request without it is
`400 max_tokens_required`. This is the client's obligation, not a server
default.

## Order

```sh
sensen-gateway admin init         "$DSN"
sensen-gateway admin seed         "$DSN" deploy/gateway/seed/sensen-org.json   # capture the key it prints ONCE
sensen-gateway admin publish-epoch "$DSN" sensen
```

`import-policy` is **not** in that list on purpose: `publishEpoch` composes
only the prelude and the shipped compliance layer, and refuses to publish at
all while any policy activation is live — *"this gateway does not load policy
layers … (not built)"*. Entitlement therefore comes from the PRELUDE over the
seeded relations (`member` → `group_cluster` → `in_cluster`), which is
sufficient: `gateway_derive_probe` shows it deriving
`permitted(olumuyiwa, serve_edge_1)` from exactly this seed.

## Verifying

`deploy/gateway/regression.sh <base-url> <bearer> [--cacert <pem>]` — 17 checks,
both directions, and the admit arm first because a gateway that refuses
everything passes a refuse-only suite.
