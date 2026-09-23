# Session 2026-09-23 — `FINANCE_REQUIRE_KEY`: the doc said `1` enforces, `1` serves everything

@author Olumuyiwa Oluwasanmi

Follows `session_2026-09-22_key_quota_local_gates.md`, which measured both key
gate modes locally and closed by listing `FINANCE_REQUIRE_KEY` as deliberately
NOT changed. Asked this session to "fix the FINANCE_REQUIRE_KEY issue".

There were two issues, and the one worth fixing was not the one the request
was about.

## 1. The documented value was wrong, in the direction that hides itself

`docs/API_SECURITY.md` §5 listed the rollout stages and gave Enforce as:

```
3. **Enforce.** `FINANCE_REQUIRE_KEY=1`. Unkeyed calls refused.
```

`api_key.cpp`'s `load()` maps `"1"` and `"warn"` to **`Mode::Warn`**, which
SERVES every request and only logs what Enforce would have done. So an operator
following the security document — the one you read when deciding to switch the
gate on — would have set `1`, seen the gate "on", and refused nobody. The boot
banner would have read `WARN (serving, logging what enforce would do)` and every
probe would have come back green.

**A control that is off produces passing results, not failing ones.** Same shape
as the dotenv quote-stripping defect found the day before, in a different
costume: there the shell disabled the control, here the documentation did.

**One stale copy beside three right ones.** `FINANCE_API.md`, `BUSINESS_API.md`
and `MORTGAGEFV_INTEGRATION.md` all carried the correct `1`/`warn` → served
mapping throughout, so this was never a design change that one file missed.
`pro_gate_mode()` has the identical mapping and was swept — its documentation is
correct everywhere.

## 2. Nothing gated the mapping, which is why it could drift

`grep -rn FINANCE_REQUIRE_KEY backend/tests/` returned **nothing**. The mapping
that decides whether the engine's only authentication gate refuses anybody was
entirely ungated.

`auth::parse_require_mode(std::string_view) -> Mode` is extracted from `load()`
as a **pure** function and exported. Pure deliberately: `KeyRegistry` is a Meyers
singleton that reads the environment once at first use, so one process holds one
mode for its life — pinning three modes through the registry would cost three
separate binaries, which is exactly why `test_state_assumptions_gate` is its own.

Section 10 of `test_api_key_entitlement`, **15 checks, 75 → 90**. It asserts the
two spellings of each mode, that `"1"` is Warn and specifically `!= Enforce`,
that the empty string (production's value) is Observe, and that seven
unrecognised spellings — `Enforce`, `ENFORCE`, `true`, `yes`, `on`, `3`,
`" enforce"` — all fall back to **Observe**, so a typo in a deploy variable
cannot start refusing production traffic.

Mutation-checked with `CCACHE_DISABLE=1`: making `"1"` return `Mode::Enforce`
reproduces the documented lie and fails **exactly those 2 checks and nothing
else**.

## 3. Enforce is not reachable, and the variable's NAME is what hides it

The request was presumably to turn the gate on. Measured, three independent
facts:

| # | fact | evidence |
| --- | --- | --- |
| 1 | this is an **engine-wide** switch, not a Finance one | `KeyRegistry::authenticate()` is called by `finance_service.cpp`, `calculator_service.cpp`, `assistant_service.cpp` and `mortgage_assistant_service.cpp` |
| 2 | **one key exists**, and it is not the calculator's | `FINANCE_API_KEYS` holds exactly one entry, `mortgagefvcalculator`, `tier partner`, origin-locked |
| 3 | the calculator **sends no key** | `licence.ts::authMetadata()` sets `x-api-key` only `if (info)` — a Pro *licence*. Anonymous visitors get `{}` |

Under Enforce, `Outcome::NoKey` → `UNAUTHENTICATED`. So the flip refuses **every
anonymous visitor to optionsandfuturescalculator.com, across all four
services**, while mortgagefvcalculator.com — server-side, key attached, no
`Origin` header — keeps working perfectly.

**The site that breaks is not the one the variable is named after.** That
asymmetry is the whole reason this is worth writing down: a reasonable operator
reads "FINANCE_REQUIRE_KEY", thinks about the Finance API and its one keyed
partner, and does not think about the calculator's anonymous traffic at all.

The calculator's "own publishable key" existed only in `authMetadata()`'s comment
and in `API_SECURITY.md`'s own prose — never issued, never configured, never
sent. That paragraph is corrected rather than deleted, because the intent is
still right and it is the precondition for ever enforcing.

## Decision

Asked, and the answer was **stop here**: production stays in Observe. The
Finance surface is deliberately public and the Pro gate is what protects the
assistants, so enforcing buys little and costs an outage until the calculator
carries its own key. Nothing was changed in `config/.env` or on Railway.

## Gates

| gate | result |
| --- | --- |
| `ninja build_tests` | rc 0 |
| `ninja` (engine) | rc 0 |
| `ctest` | **117/117** |
| `test_api_key_entitlement` | **90 checks, 0 failures** (was 75) |
| mutation: `"1"` → Enforce | fails exactly 2 checks, both naming the defect |

## Left undone deliberately

- **Issuing optionsandfuturescalculator.com a publishable key** and shipping it
  in the frontend bundle. It is the precondition for enforce and was offered;
  the answer was to stop. It stays recorded in `API_SECURITY.md` §5 as the one
  thing standing between today and a working `enforce`.
- **`FINANCE_REQUIRE_KEY` was NOT set to `warn` either.** It is zero-risk, but
  the question it answers — who is calling unkeyed — is already answered: every
  anonymous visitor to the calculator. It would buy log noise, not data.
