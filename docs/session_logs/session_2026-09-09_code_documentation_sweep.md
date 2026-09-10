# 2026-09-09 — documenting the whole codebase, and what verifying the documentation found

@author Olumuyiwa Oluwasanmi

**Trigger:** "document all of the code and update the documentation for the
features."

Twelve documentation jobs were run against the tree, each told to read the source
and cite `path:line` for every specific claim, each writing exactly one file. The
output was then verified mechanically and by sampling before any of it was kept.
The verification is the part of this log worth reading: the documents are good,
and the three places they were wrong are wrong in ways that generalise.

## What was produced

| document | covers |
| --- | --- |
| `docs/FEATURES.md` | as-built feature inventory for both products, including what does NOT exist |
| `docs/api/GRPC_SURFACE.md` | all four protos, 55 RPCs, every message, units and transcoder paths |
| `docs/architecture/CODE_MAP_BACKEND_SERVICES.md` | `main.cpp` boot order and every RPC's delegate, gate and status codes |
| `docs/architecture/CODE_MAP_BACKEND_VERIFICATION.md` | the five grounding gates in execution order, every static table |
| `docs/architecture/CODE_MAP_BACKEND_PLATFORM.md` | keys, quota, Postgres, stores, queues, market data, FIPS |
| `docs/architecture/CODE_MAP_FRONTEND.md` | routes, stores, the staleness token, components, the build gate |
| `docs/architecture/CODE_MAP_EDGE_AND_CLIENTS.md` | billing Worker, vendored client, migrations, deploy scripts, Envoy |
| `docs/architecture/CODE_MAP_ML_AND_SCRIPTS.md` | dataset, training, evaluation harnesses, probes, `scripts/` |
| `docs/architecture/TEST_INVENTORY.md` | one row per gate, and what breaks if it is deleted |

Updated: `docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md`, `docs/FINANCE_API.md`,
`docs/INDEX.md`.

## The verification, and its numbers

**2,527 `path:line` citations resolve** — the file exists and the line is inside
it. Zero cite a line past end of file, after 22 range-ends were clamped.

Ground truth was pulled BEFORE the documents arrived, so checking was a diff
rather than a re-read: `kLabelSpace` 184, `kOperationIds` 27, `kConventionValues`
38, `kVariantInertFields` 13, `kMoneyFields` 48, `kUngroundedFields` 1,
`kUnitCappedRatioFields` 5. Every one matched.

## The fabrication clustered where the code was THINNEST

`TEST_INVENTORY.md` described `backend/src/testing/testing_framework.cppm`
exporting `TestRegistry`, `TestCase`, `RegisterTest`, `CHECK`, `REQUIRE` and
`CHECK_THROWS_AS`. **None of that exists.** The real file is
`backend/src/modules/testing_framework.cppm`, 53 lines, exporting exactly
`TestResult`, `assert_true` and `run_all_tests`. It also invented
`api_key_store.cppm`, `licence.cppm` and `submodules/sensen-client/proto/finance.proto`.

The 2,903-line `mortgage_verification.cppm` was documented without a single
error. The 53-line testing module — too small to carry the GTest-shaped API a
reader expects — is where the gap got filled from expectation instead of from the
file. **Thin files are the risk, not big ones.** Corrected by re-running that one
job with the measured facts handed to it as corrections; the fabricated section
is gone and every path it cites now exists.

## An edge function that is checked in, called by nobody, writing to no table

`FEATURES.md` first said "there are no Supabase Edge Functions in this
repository." There is one: `supabase/functions/lead-attribution/index.ts`. But
**nothing calls it** — no reference to `lead-attribution` or `functions/v1`
anywhere under `frontend/src`, `workers/` or `clients/` — and **no migration in
this repository creates the `leads` table it inserts into**;
`supabase/migrations/20260723000000_initial_schema.sql:150` mentions leads only
in a comment.

Both the original claim and the naive correction would have been wrong. The model
reasoned from "no callers" to "no file"; a reader who found the file would have
concluded the feature ships. It is neither. Recorded as **wired to nothing**.

## Two rows of `docs/INDEX.md` were stale, and both were corrected by measurement

- It said the frontend is on **Cloudflare Pages**. It is Workers static assets —
  the error CLAUDE.md records as costing five "completed" deploys that changed
  nothing a user could see.
- It said billing was **not yet deployed**. Measured today:
  `GET https://ofc-billing.muyiwamc2.workers.dev/health` returns
  `200 {"ok":true,"supabaseConfigured":true}`. It is deployed and holds its
  Supabase credentials. What remains unproven is a live Stripe checkout round
  trip — a different claim, and still recorded as unproven.
- Its native-gRPC paragraph still said "no request reaches the container", which
  is false and was corrected in CLAUDE.md months ago: the request reaches the
  engine and the engine answers, and Railway's HTTP edge strips the trailer that
  carries `grpc-status`.

## Eight finance RPCs were documented nowhere

`ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
`ComputeHomeFutureValue`, `ComputeHomeNpv`, `ComputeRentVsBuy`,
`ComputeRentVsBuyBatch` and `ComputeClosingCosts` appeared nowhere in
`docs/FINANCE_API.md` — the whole mortgagefv surface, absent from the document a
partner reads. Now documented, 538 → 1,213 lines, including the two request
shapes `ComputeRentVsBuy` carries and how the service decides between them.

## The PRD violated the repository's own authorship policy

Its header read `Author / Lead Architect: Antigravity (Pair Programming with
User)`, and its approvals table attributed system architecture to tooling.
`config/update_policy.txt` forbids exactly that. Both now name Olumuyiwa
Oluwasanmi. The roadmap was also ticked honestly for the first time since July —
each shipped item annotated where it shipped DIFFERENTLY from the plan (Alpaca
rather than Yahoo, 2D Canvas rather than WebGL, Stripe subscriptions rather than
broker CPA), and each abandoned item said so instead of sitting unchecked.

## AUTO-CORRECTING THE CITATIONS MADE THEM WORSE, AND IT WAS REVERTED

A sample of 1,167 citations asked whether the identifier named just before a
citation appears within eight lines of the cited line. 18.6% did not. A script
was then written to "fix" the certain subset: cases where the identifier is
absent near the cited line AND occurs exactly once in the whole file, so there is
only one candidate.

It changed 14 citations and **several of them had been right**. The worst:
`useCalculatorStore.ts:437` is `let calculationSeq = 0;` — verified by hand
earlier the same session — and the script moved it to `:422` because the
preceding prose contained the word `useEffect`, which happens to occur once. The
heuristic was reading the sentence, not the claim.

All 14 were reverted. The residue was left alone and a note added to each
document instead: **the path and the symbol name are reliable; the line number is
a pointer to a neighbourhood.** A precise-looking number that is wrong is worse
than an honest range, and a script confident enough to rewrite citations needs to
be right more often than the citations it is rewriting.

## What was NOT done

- No code changed. `scripts/code_policy_check.sh` passes; its two trailing-return
  warnings on `smoke_client.cpp` predate this work.
- Line-level accuracy was sampled, not exhaustively verified. Doing that properly
  needs a checker that parses the claim, not one that greps near it — which is
  the same lesson the reverted script taught, stated as future work.
