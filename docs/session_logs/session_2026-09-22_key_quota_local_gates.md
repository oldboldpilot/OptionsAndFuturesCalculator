# Session 2026-09-22 — exercising the key and quota gates locally, and the loader that was deleting them

@author Olumuyiwa Oluwasanmi

Follows `session_2026-09-22_sensen_bump_both_sites.md`, which closed by listing
three things NOT exercised locally: the Pro gate's ADMIT direction, quota, and
`FINANCE_API_KEYS` auth — the latter two "because those JSON values do not
survive shell-sourcing `config/.env`". That sentence was correct and was
treated as a constraint. It is a DEFECT IN THE LOADER, and once that is fixed
all three are ordinary to exercise.

## The root cause: the shell deletes the quotes, and nothing says so

`FINANCE_API_KEYS` and `QUOTA_POLICY` are bare JSON in `config/.env`. The value
is unquoted, so `set -a; . config/.env` treats every `"` as a quoting
metacharacter and removes it during word expansion.

Measured, not inferred:

| variable | in the file | after sourcing | difference |
| --- | --- | --- | --- |
| `QUOTA_POLICY` | 316 | 286 | exactly its 30 double quotes |
| `FINANCE_API_KEYS` | 319 | 293 | exactly its 26 double quotes |

`{anonymous_tier:anonymous,...}` is not JSON, so the engine logs
`... is not valid JSON; quotas stay DISABLED` and serves every request happily.

**The engine that had been running since the previous session was in exactly
that state**, confirmed by reading `/proc/<pid>/environ` directly:
`FINANCE_API_KEYS len=293`, `QUOTA_POLICY len=286`. So every local measurement
taken against it was taken against an engine with **both controls switched
off**, while `ps`, the logs and every smoke suite looked perfectly healthy.

**Railway is NOT affected**, and checking that mattered before touching
anything: it sets the container environment directly with no shell in the path.
Both variables are 319 and 316 bytes there. This was only ever a local-loader
defect — which is why it survived so long, because production was right.

`scripts/run_with_env.py` parses the file and `execve`s, so no shell sees the
values. Its parser is deliberately NOT shell-compatible: no expansion, no
interpolation, one optional layer of surrounding quotes stripped. Anything
cleverer reintroduces the class of bug it exists to avoid.

## What the gates actually do, both directions

`scripts/probe_key_and_quota.py`, native gRPC, hand-encoded protobuf so
`x-api-key` and `origin` are under the probe's control.

### API-key auth — 6/6 Observe, 7/7 Enforce

| case | Observe (**production's setting**) | `FINANCE_REQUIRE_KEY=enforce` |
| --- | --- | --- |
| valid key, no origin | OK | OK |
| valid key + allowed origin | OK | OK |
| valid key + FOREIGN origin | **OK** | `PERMISSION_DENIED` not registered for this site |
| bogus key | **OK** | `UNAUTHENTICATED` unrecognised API key |
| no key | **OK** | `UNAUTHENTICATED` no API key supplied |

`FINANCE_REQUIRE_KEY` is unset in `config/.env` **and on the Railway service**,
so Observe is production parity. The honest statement is that on the finance
surface the key is IDENTIFICATION AND METERING, not access control, and that
an origin-locked publishable key **is not origin-locked in Observe** because the
binding is only consulted on the enforcing path. That is consistent with the
documented design — the Finance RPCs are deliberately ungated — but it is worth
writing as what it is rather than as what the key file's `origin` lines imply.

### The two gates are ORDERED, and the first probe run got it wrong

Under Observe an anonymous `ParseOperation` is refused by the PRO gate, whose
message points the caller at the free Finance RPCs. Under Enforce the KEY gate
refuses first and that redirection is never reached. Both correct. The probe
asserted the `PERMISSION_DENIED` shape unconditionally and therefore reported
the enforcing engine as broken — a test written for one mode, run against the
other. Now mode-aware, and it asserts the ORDER rather than just the code.

### The ADMIT direction, which is the half that was actually missing

The issued key is `tier partner, scopes [finance, assistant]`, so with it
`ParseOperation` passes BOTH gates and stops at inference — `DEADLINE_EXCEEDED`
locally, because no weights are present. The assertion is
"not `PERMISSION_DENIED`/`UNAUTHENTICATED`", never a success payload: what comes
back after the gates is a statement about the MODEL, not about entitlement.
Asserting a payload would make the check depend on having weights locally, which
is the thing that made this direction look untestable in the first place.

### Quota — 6/6, and the counts reconcile exactly

A deliberately tiny policy (anonymous 4/min, partner 10/min), 14 requests each way:

| caller | served before refusal | refusal |
| --- | --- | --- |
| unkeyed | 1 | `quota exceeded for tier 'anonymous' on ComputePayment (request rate); retry in 15s` |
| partner key | 7 | `quota exceeded for tier 'partner' on ComputePayment (request rate); retry in 6s` |

**The arithmetic is the finding.** 4 minus the 3 earlier unkeyed probe calls is
1; 10 minus the 3 earlier keyed calls is 7. Two separate per-caller buckets,
metered exactly, with the tier taken from the KEY REGISTRY even though
`QUOTA_API_KEYS` is unset (`Quotas ENABLED: 4 tiers, 0 keys`) — so
`QUOTA_API_KEYS` is a second, independent way to assign a tier and not the only
one. A burst run only anonymously would have passed against an engine that
metered every caller identically.

The policy's top-level key is `anonymous_tier`, not `default_tier`.

## AVX-512 on the Railway deploy (asked mid-session)

Two answers, and only quoting both is honest.

- **The host supports it comprehensively.** AMD EPYC 9655 (Zen 5), 48 vCPU:
  `avx512f/bw/cd/dq/vl/ifma/vbmi/vbmi2/vnni/bitalg/vpopcntdq/vp2intersect`
  **and `avx512_bf16`**, plus `avx_vnni`.
- **The baseline is `x86-64-v3` — AVX2 — on purpose**, for cross-host FP parity
  and durable-replay determinism. `CANONICAL_FLAGS` does not pass `-mavx512f`.
- **AVX-512 is still compiled in and reached by runtime CPUID dispatch.**
  Verified on the binary: **11,198** `%zmm` references, **143** avx512-named
  symbols, 52 `vpdpbusd` (AVX512-VNNI int8 dot product) and 66 `vmovdqu64`.
  The `-march` baseline sets the FLOOR, not the ceiling.

Limit stated: the engine logs no SIMD tier at boot, so the rung selected inside
the container has not been observed directly. Three joined facts, not one
measurement.

## Gates

| gate | result |
| --- | --- |
| `ninja -C backend/build -n` | no compile steps — only `.py` added, engine binary untouched |
| `ninja build_tests` | no work to do |
| `ctest` | see below |
| `probe_key_and_quota.py` Observe | **6/6** |
| `probe_key_and_quota.py` Enforce | **7/7** |
| `probe_key_and_quota.py` quota burst | **6/6** |

## Left undone deliberately

- **`FINANCE_REQUIRE_KEY` was NOT changed** in `config/.env` or on Railway.
  Turning the key gate to enforce is a product decision with a blast radius —
  every unkeyed caller of the public Finance API starts failing — and nothing
  asked for it. The measurement above is what that decision would need.
- **No boot-time SIMD tier log was added.** It would close the AVX-512 gap
  properly, but it is a change to engine startup on the back of a question.
- The second engine used for the enforce and quota arms was stopped; the
  `:50051` engine is left running, now with the env loaded CORRECTLY, which is
  itself an improvement on the state this session found it in.
