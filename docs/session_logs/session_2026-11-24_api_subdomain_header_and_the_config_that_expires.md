# 2026-11-24 — the API subdomain gets its header, and the config file that stops being read in six days

Three things: a commit from this morning that had never been deployed, a documented answer that
belonged to a previous arithmetic era, and the go-to-market plan for the finance API — which turned
out to need one measurement more than it needed prose.

---

## 1. The header was committed, pushed, and not deployed

`f9bc97a` (00:10 today) added an `x-robots-tag` of `noindex, nofollow` to the virtual host in
`backend/envoy.yaml`, because Search Console showed the API subdomain carrying impressions with
nothing telling Google to leave it alone.

Asked whether the changes were deployed, the honest answer needed a probe rather than a memory:

```
HTTP/2 200
date: Wed, 25 Nov 2026 01:53:36 GMT
server: railway-hikari
```

No such header: absent on the root path, on `/healthz`, and on a third path. The engine's active deployment was
timestamped `2026-11-23T04:32:50Z` — before the commit existed. So: committed, pushed to both
remotes, and never deployed. **A config-only change still needs the whole image**, because
`backend/envoy.yaml` is baked in at build time rather than read at boot, and that is exactly the
kind of change it is easy to believe shipped itself.

Deployed from the dedicated checkout, which was one commit stale and brought forward first. All
three preconditions checked before the archive was built, because the deploy tars the working
directory: HEAD is the pushed commit, the tree is clean, every submodule sits on its pin with no
leading marker. Upload `http=200` in 4.2 s, and the builder confirmed as Dockerfile rather than the
default.

---

## 2. The documented answer was from the `__int128` era

While reading the customer-facing docs for §3's plan, the onboarding runbook's example output did
not match what the API returns. Measured:

| | |
| --- | --- |
| documented | `-1798.651575458257198999` |
| returned today | `-1798.65157545825718377438373105342774511169` |

The money width moved from 1e18 to 1e38 on 2026-09-23 and the examples stayed put. **The old value
is not a prefix of the new one** — they diverge at the seventeenth decimal place, so a reader
comparing against the documented string gets a mismatch rather than a shorter match.

**The census was wrong the first time, and for an embarrassing reason.** The first count said ten
occurrences in four files; it was `grep | head`, and `head` stops at ten lines. The real count was
eighteen across seven files. A truncated census is worse than no census, because it looks like a
measurement.

**The discriminator is what the line claims, not which string it holds.** Thirteen of the eighteen
were left exactly as they were:

- dated records — "Measured against production on 2026-08-05", "Verified over the TCP proxy on
  2026-08-29", "before this hardening, measured against production". On those dates the engine did
  return that. Rewriting a dated measurement to match today falsifies it.
- two request **inputs**, where an eighteen-place payment is a perfectly good decimal to send.

Five were present-tense claims about what the API returns, and got the measured value.

**Two were worse than stale and deliberately did not get a new number.** The integration guide told
an integrator, twice, to "assert the value is exactly" that literal. That assertion fails today —
and CLAUDE.md's own rule is never to pin the digit count of a money string, so swapping in a fresh
thirty-eight-place literal would leave the same trap armed for the next width change. Both now say
to compare the decimal value at a tolerance and to assert the **sign**, which is what those
assertions were really guarding.

Two citations then failed `tools/check_docs_claims.py`, both on lines this change had touched. The
second is the one worth keeping: the code map cited README **line ranges**, and editing that README
moved them. It now cites the two section headings, which an inserted paragraph does not move.

Agent skills installed by tooling are now ignored, following the browser-tool directory's precedent one
line above. Not housekeeping: the checker reads untracked Markdown, so thirty-seven vendor files
were contributing 891 MISSING citations to a gate CLAUDE.md requires to be 0, and a gate that is
red for reasons nobody will fix stops being read. The gate reports fifty-four citations checked and none missing after.

---

## 3. Selling the API needed one measurement more than it needed prose

`docs/API_GO_TO_MARKET.md` covers banks, mortgage companies and real-estate professionals, plus a
fourth segment that was not in the brief and is cheapest to serve — publishable keys bound to an
origin make this a drop-in browser widget business as well as a server-side API.

Two findings decided the shape of it.

**The arithmetic is not the moat.** Any mortgage company can get amortization from a free package
in an afternoon. What cannot be casually rebuilt is the per-state tax and insurance data with
provenance, the single backend owner for survey rates, and the batch RPCs that answer a loan book
or fifty states in one call. So the plan leads with the data and lets the forty-eight RPCs be the
proof of depth.

**Enforcement is off in production**, which is phase 0 and the reason the plan does not open with
outreach:

| request | result |
| --- | --- |
| valid body, no key | `HTTP 200`, correct answer |
| valid body, forged `sk_live_` key | `HTTP 200`, correct answer |

The machinery is all built — `issue-key` mints secret and publishable keys and stores only a digest,
and the quota meters two axes with a retry-after derived from the bucket's own refill rate. None of
it is switched on, and a forged key is **not refused**, so there is no enforcement to bypass rather
than a lenient policy. A paid tier means nothing while the free tier is unlimited and anonymous,
and there is no usage data at all: there may already be callers and we cannot name one.

The onboarding runbook documents the `401` an unkeyed call would receive. That is accurate about the
enforcing posture and silent about production not being in it — the same shape as §2.

---

## 4. `railway.json` stops being read on 2026-12-01, and three settings live only there

The CLI warns that config-as-code is deprecated and that existing files keep working until
**2026-12-01**. Six days. That warning is easy to scroll past, so the exposure was measured rather
than assumed, by asking what the service's own persisted state holds — where a null means the value
is not set there and comes from the file instead:

| setting | `railway.json` | persisted on the service | consequence after 2026-12-01 |
| --- | --- | --- | --- |
| `builder` | `DOCKERFILE` | Railpack | the build stops producing the engine image |
| `numReplicas` | 2 | null | one replica; capacity halves |
| `healthcheckPath` | `/healthz` | null | no healthcheck at all |
| `restartPolicyMaxRetries` | 3 | 10 | none — the file's 3 is already fiction |

The builder row is the dangerous one, and today's deploy log shows why it is real: the deployment
initialises as Railpack and is then flipped to Dockerfile. That flip is what the file is doing, so
losing the file loses the flip — and a Railpack build of this repository does not fail in a way that
says "your builder changed".

Two more consequences worth naming. Losing the healthcheck makes an already-known failure mode
worse, because this platform reports SUCCESS while a container crash-loops and the healthcheck is
what distinguishes them. And losing the second replica silently invalidates the cutover check that
`scripts/railway_deploy.sh` prints, which tells the operator to expect one loaded-model line **per
replica** — it would read two as wrong when one had become correct.

**Not migrated in this session, deliberately.** A deploy was mid-build from this very file, and
changing how deploys are configured while one is in flight produces failures nobody can attribute.
The migration is a deploy-behaviour change and has to be proved by a deploy of its own.

---

## Still open

- **Migrate the Railway config before 2026-12-01**, then prove it with a deploy that shows the
  Dockerfile builder and two replicas. Deadline is real and six days out.
- **Phase 0 of the API plan**: switch enforcement on, with the existing site's keys issued first,
  and write down how many distinct callers appear. Nothing else in that plan matters first.
- The engine deploy's cutover still to be confirmed by the live header, which is the only end-to-end
  test of it.
