# Session log — 2026-08-28 (second) — TLS enforced, RLS completed, a FIPS gate, a batch RPC

@author Olumuyiwa Oluwasanmi

Prompted by a direct question: do we have RLS, HIPAA, financial-services and
FIPS security? The answer was measured rather than recalled, and it was mostly
"no". What follows is what was true, and what was then built.

## What was actually in place (measured)

```
saved_strategies | rls=t force=t 1 policy
inference_jobs   | rls=f force=f 0
users            | rls=f force=f 0
profiles         | rls=f force=f 0
auth.users       | rls=f force=f 0
```

DB connection negotiated TLSv1.3 — but `DATABASE_URL` carried **no sslmode**, so
libpq's `prefer` default applied: opportunistic, silent plaintext fallback.

Zero compliance claims anywhere in the tree (the only grep hits were `OccParts`
matching "CCPA"). No FIPS provider configured.

## Built

**TLS enforced.** `sslmode=require` merged as a later `PQconnectdbParams`
keyword, so it beats `DATABASE_URL`. Tested every level against production:
disable/prefer/require OK; verify-ca and verify-full FAIL (no CA bundle in the
image). `require` encrypts but does not verify the certificate — that limit is
documented, not implied.

**RLS completed.** Migration 06 covers `public.users` and `public.profiles`.
Proven to FILTER: postgres 2 rows → `ofc_app` with subject 1 row → cross-user
row 0 → **no subject set, 0 rows** (fail-closed). Both tables empty today, so
this closed a latent exposure before first signup.

**FIPS gate, deliberately not a FIPS claim.** `FIPS_MODE=off|preferred|required`.
`required` loads the `fips` + `base` providers, pins `fips=yes`, and **exits 1
before binding** if that fails — proven by forcing an empty `OPENSSL_MODULES`.
No emitted message says "certified", "validated" or "compliant".

Four facts bound the claim: stock `ubuntu:24.04` ships **no** fips provider
(only `legacy.so`); public TLS is Railway's edge and `envoy.yaml` has no TLS
config, so nothing reaches past "inside the container"; the engine has exactly
ONE TLS stack (`gRPC_SSL_PROVIDER=package` FORCEd, `nm` shows zero defined
`SSL_*`); and every algorithm in use is already FIPS-approved across a
two-file surface, with sensen doing no crypto at all. The gap is module
validation, not algorithm choice.

**Batch RPC.** `ComputeRentVsBuyBatch`, ≤1000 scenarios, charged per scenario,
per-row outcomes, positional results, one shared implementation with the
single-scenario RPC.

| n | serial | parallel only | shipped |
| --- | --- | --- | --- |
| 1 | 136us | 151us | **103us** |
| 10 | 163us | 230us | **130us** |
| 100 | 686us | 446us | **519us** |
| 500 | 3275us | 1250us | **1244us** |
| 1000 | 5736us | 1826us | **1909us** |

**Parallel alone was SLOWER below ~64 scenarios** (n=10: 0.7x). A serial
threshold is why the shipped column beats both. Gate is byte-identity —
aggregate SHA `f22e8f083630c4e4` on both paths — not a timing.

## Two things worth carrying forward

**The drift check failed in THREE places** when the batch RPC was added:
`EXCLUDE_RPCS`, `test_mortgage_grammar`, `test_mortgage_verification`. The
four-tables lesson, working. It is excluded on purpose — a bulk API is not an
utterance.

**Programmatic SEO is the AdSense violation at scale.** Generating pages from
one template with substituted numbers is exactly what got the site flagged on
2026-08-16. And `check-export.mjs`'s duplicate check is EXACT STRING EQUALITY,
so near-duplicates differing only in numbers pass green. The guardrail does not
cover the case. `MIN_WORDS=600` and the `/guides/` ad allowlist still hold.

## Gates

ctest 100/100 (zero stray engines on :50051). `smoke_client … finance` passes,
verifying rent-vs-buy against a **closed form** (`advantage 53004.31 (closed
form 53004.31)`), not against the engine's own prior output.
