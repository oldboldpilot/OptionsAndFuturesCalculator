# State assumptions: backend handoff

@author Olumuyiwa Oluwasanmi

**Status: live.** The weekly US Census ACS refresh now runs in the finance
backend. `public.state_assumptions` is populated for all fifty states from ACS
5-year **2024** — a vintage newer than the one the original handoff specified,
for a reason worth reading in §7.

This document is what the web app needs to switch over. It covers the two new
RPCs, what each field means, what a refusal looks like, and the four app-side
steps that complete the cutover.

---

## 1. What moved

| | before | now |
| --- | --- | --- |
| the job | `src/lib/state-refresh.server.ts`, pg_cron Monday 06:00 UTC | in the backend engine, weekly, both replicas, single-flighted |
| the table | app's Supabase | this backend's Railway Postgres |
| the read | direct table read | `GetStateAssumptions`, or keep reading a table you replicate |
| the trigger | admin button → app route | admin button → `RefreshStateAssumptions` |

Two RPCs were added to **`sensen.finance.Finance`** — the same service the app
already calls for `ComputePayment`, `ComputeAmortization` and the rest. No new
host, no new port, no new credential for reads.

Base URL: `https://api.optionsandfuturescalculator.com`

---

## 2. `GetStateAssumptions` — the read

Open to everyone, no credential. The table holds public Census aggregates;
there is nothing to authenticate.

```
POST /sensen.finance.Finance/GetStateAssumptions
Content-Type: application/json

{}                      → all fifty states
{"slug": "texas"}       → one state
```

```json
{
  "states": [
    {
      "slug": "texas",
      "name": "Texas",
      "abbr": "TX",
      "medianPrice": "283800.00",
      "propertyTaxRate": "1.49",
      "insuranceAnnual": "",
      "stateIncomeTax": "",
      "medianRent": "1403.00",
      "note": "",
      "dataSource": "US Census ACS 5-year 2024",
      "dataYear": 2024,
      "refreshedAt": "2026-08-29T11:04:22Z"
    }
  ]
}
```

An unknown slug returns `{"states": []}`, not an error — asking about a state
that is not there is a legitimate question with the answer "none".

### Every money and rate field is a STRING

This is the file-wide convention on `finance.proto` and it is not stylistic.
The engine computes in `BigDecimal` (exact `__int128` fixed-point, eighteen
places) and JavaScript's `number` is a float64, so a JSON number would be
rounded before your code ran. `"283800.00"` is exact; `283800.00` as a number is
a value your runtime chose.

Parse at the point of display, not at the point of receipt. `Number(x)` for a
chart axis is fine; `Number(x)` on the way into state and back out again is how
a cent goes missing.

### `insuranceAnnual`, `stateIncomeTax` and `note` come back empty unless set

They are **editorial** — the ACS does not publish them, the refresh never
writes them, and the database physically cannot let it (see §6). Empty means
nobody has authored one, not that the refresh failed. Keep your existing
bundled fallbacks for these three.

### `refreshedAt`: empty is a state, and it is not the epoch

RFC3339 UTC, or **the empty string** when no successful refresh has ever
touched that row. It is a string rather than a `Timestamp` for exactly this
reason — an absent value has to be distinguishable from `1970-01-01`, and with
a numeric timestamp it is not.

Treat empty as "unknown", never as "very old". The site's option-chain LIVE
badge exists because that distinction was got wrong once already: a freshness
indicator derived from a missing timestamp reads as fresh or as ancient
depending on which way the arithmetic falls, and both are fabrications.

**An aborted run does not touch `refreshedAt`.** If the ACS is unusable this
week, the field keeps last week's instant and the numbers keep matching it.
Bumping a timestamp on a run that wrote nothing would make every staleness check
downstream lie, which is worse than being visibly stale.

---

## 3. `RefreshStateAssumptions` — the trigger

**Requires the partner key — the one you already hold.** The key issued to
mortgagefv on 2026-08-10 (`tier partner`, `scopes finance, assistant`) is the
same credential you send on `ParseOperation`, and it works here unchanged. There
is no new secret to provision.

This is the one write on the finance surface and it is a server-side call. Do
not put the key in the browser bundle; route the admin button through your own
server, which presents it.

Both directions were verified against production on 2026-08-29 — anonymous is
refused with code 7, and the partner key returns a full dry run. Testing only
the refuse direction proves nothing: a gate that refuses everyone passes it.

```
anonymous  → 403  {"code":7,"message":"Refreshing state assumptions requires a
                   partner credential. Reading them (GetStateAssumptions) does not."}
partner    → 200  {"ok":true,"dataYear":2024,"statesUpdated":50,
                   "statesRejected":0,"dataSource":"US Census ACS 5-year 2024"}
```

```
POST /sensen.finance.Finance/RefreshStateAssumptions
Content-Type: application/json
x-api-key: <the partner key, server-side only>

{"dryRun": true}                      → fetch, validate, report, write nothing
{}                                    → the real thing, newest usable vintage
{"dataYear": 2023}                    → pin a vintage
```

```json
{
  "ok": true,
  "dataYear": 2024,
  "statesUpdated": 50,
  "statesRejected": 0,
  "dataSource": "US Census ACS 5-year 2024",
  "error": ""
}
```

### `dryRun` is the button you want behind "check for new data"

It performs the whole run — fetch, parse, validate, compute what would change —
and rolls back. It is how an operator inspects a new ACS vintage before
trusting it, and it is free of consequences.

### A refusal arrives as HTTP 200 with `ok: false`

This is deliberate and it is the contract the mortgage assistant already uses.
A caller has to be able to tell "the site is serving last week's numbers because
the ACS was unusable" apart from "the RPC did not happen", and a transport error
collapses those into one thing. So `error` carries a sentence naming what
happened, and the transport codes stay reserved for what they mean:

| status | means |
| --- | --- |
| `OK` + `ok: true` | it ran and wrote |
| `OK` + `ok: false` | it ran and refused — read `error`, existing data is untouched |
| `PERMISSION_DENIED` (7) | no partner credential |
| `FAILED_PRECONDITION` (9) | no `CENSUS_API_KEY` on this deployment |
| `ABORTED` (10) | another replica is mid-run; retry shortly |
| `UNAVAILABLE` (14) | the database is unreachable |

Route on the **code**, never on the message text. Every message in this file has
been reworded at least once; the codes have not.

### `statesRejected` is the number to surface

A run that quietly updated 41 of 50 states looks exactly like a healthy one if
you only render `ok`. Show both counts on the admin page.

---

## 4. Cutover, four steps

1. **Repoint `loadStateGuides()`** at `GetStateAssumptions`. Keep the bundled
   `STATE_GUIDES` fallback and keep the cache — a five-minute TTL is right for
   data that changes annually.
2. **Point the admin "refresh now" button** at `RefreshStateAssumptions`,
   through a server route holding the partner key. Render `statesUpdated`,
   `statesRejected`, `dataYear` and `error`.
3. **Delete `src/lib/state-refresh.server.ts` and the pg_cron schedule.** Do
   this last, and only after step 1 has been live long enough to be sure. Two
   jobs writing the same table is not harmful here — both write the same Census
   figures — but the pg_cron one writes a table the site is no longer reading,
   which is a confusing thing to leave running.
4. **Nothing changes for `insurance_annual`, `state_income_tax` or `note`.**
   They stay yours.

### The acceptance test from the original handoff, answered

> every row has `data_source LIKE 'US Census ACS 5-year%'`, `data_year` = the
> vintage used, `refreshed_at` = run time, values within the plausibility
> bounds. The state pages must render identical numbers before/after cutover.

The first three hold by construction — they are written in the same transaction
as the values, and a run that writes nothing writes none of them. The fourth
will **not** hold, and it should not: the numbers move because the vintage moved
from 2023 to 2024. Texas is `283800.00 / 1403.00 / 1.49%` on ACS 2024. Compare
against a `dryRun` first if you want the diff before it lands.

---

## 5. Schedule

Weekly, catch-up rather than calendar: on a six-hourly tick each replica asks
"has a run succeeded in the last eight days?" and runs if not.

A calendar slot is missed whenever a deploy lands on the hour, and a missed
weekly slot is a week of staleness that nothing reports. Asking about the
outcome instead of the clock cannot miss.

Both replicas run the timer. `pg_try_advisory_lock` makes that safe — the
second replica takes the `ABORTED` path rather than duplicating the run.
Electing a leader was not an option: Railway replicas share a hostname and have
no stable identity, which is documented at length elsewhere in this repo.

---

## 6. Why you can trust what lands in the table

Three controls, and the strongest is the one that is not RLS.

**CHECK constraints** carry the plausibility bounds in the schema, so they bind
*every* writer — this code, a hand-typed psql session, a superuser. Verified:
`median_price = 5` is refused as `postgres`. An RLS `WITH CHECK` would bind only
policy-scoped roles and would not have caught that.

**A column-scoped grant.** The refresh runs as `ofc_refresh`, a NOLOGIN role
holding `UPDATE` on exactly six columns. `insurance_annual`, `state_income_tax`
and `note` are unwritable *by the database*, so a refresh cannot destroy your
editorial content even if this code tries. Verified: writing an editorial column
as `ofc_refresh` is `permission denied`, as is `DELETE`.

**Validation refuses, never clamps.** A value outside the bounds is dropped and
counted in `statesRejected`. Clamping would produce a number nobody measured
that nothing downstream can tell from one that was.

Fewer than forty usable states aborts the whole run and keeps existing data.
Stale-but-honest beats fresh-but-wrong.

**HIPAA does not apply and is not claimed.** Public aggregate Census housing
data about states: no PHI, no covered entity. What the intent translates to is
implemented — per-row provenance, an audit row per run, least privilege,
integrity at rest, and a key that is never logged.

---

## 7. Two things the original handoff got wrong, and what replaced them

**The vintage list.** The handoff specified `[2023, 2022]` with "bump the list
when new vintages ship". The candidates are derived from the clock instead —
newest first, two years back — and that found **2024**, a full vintage newer
than the hardcoded list would have fetched. A literal list rots every January,
and its failure reads as an upstream outage rather than a stale constant.

**Where the table lives.** The handoff described the table as already existing
in Supabase and the backend as writing it. This backend holds credentials for
exactly one database, its own, and the only hosted-Supabase material in its
environment belongs to an unrelated product. So the table was created here
(`backend/migrations/07_state_assumptions.sql`, fifty states seeded) and the app
repoints its reads. That is step 1 of the cutover.

---

## 8. Checking it yourself

```bash
curl -s https://api.optionsandfuturescalculator.com/sensen.finance.Finance/GetStateAssumptions \
  -H 'Content-Type: application/json' -d '{"slug":"texas"}'
```

`scripts/probe_finance_service.py` in the backend repo asserts the whole shape
against the live service — fifty states, the RFC3339-with-Z timestamp, money
fields still strings, every value inside the plausibility bounds, one vintage
across all fifty. 34/34 as of 2026-08-29.

**One of those checks is there because it failed.** `refreshed_at` shipped as
`2026-08-29T12:49:58+00` for a few hours: Postgres's `to_char ... OF` renders
the shortest offset, which is not valid RFC3339. V8 parses it leniently and
JavaScriptCore returns `Invalid Date`, so it would have worked in the browser it
was tested in and produced an empty "last refreshed" on every iPhone. Fixed to
an explicit `Z`; the probe now pins the shape.

Every RPC on this service is reachable as
`POST /<package>.<Service>/<Method>` with a JSON body — Envoy's
`grpc_json_transcoder` covers the whole surface with `auto_mapping: true`. The
same two RPCs are on gRPC-Web through the same host if you would rather use the
generated client, which you already have for the rest of the finance surface.

Regenerate from `backend/proto/finance.proto`. The copy in
`clients/mortgagefv/proto/finance.proto` is byte-identical to it and a test
fails the build if that stops being true.
