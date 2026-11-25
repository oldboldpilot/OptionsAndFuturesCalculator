# 2026-11-24 (evening) — the config migrated, a paywall that punished signing up, and a tag that was not a version

The morning's log ended with three open items. This is what happened to them, plus two things nobody
had asked about that turned out to matter more than what had been asked.

Every section here follows the same shape, because the evening kept producing it: a statement that
was true when written, left standing while the world moved. Three of the instances were ours, written
hours earlier.

---

## 1. The config migrated, and the status command said there was nothing to migrate

`railway.json` stops being read on 2026-12-01. The morning's log recorded three settings living only
there and deferred the migration because a deploy was mid-build from that very file.

**The exposure was four services, not one.** `resolvedFileConfig.propertyFileMapping` on a service
instance names which properties Railway takes from a config file. For the engine it is all nine. The
same query showed `sgee-queue-1`, `sgee-queue-2` and `sgee-queue-3` each resolving their own
/railway.json too, from deployments dating to 2026-09-30.

**railway config migrate status reports "No services read Config as Code", and it is wrong.** It
inspects the explicit `railwayConfigFile` field, which is null on every service here, and is blind to
a file DISCOVERED at the root of an upload — which is how all four of these services actually get
theirs. railway config migrate in the very same CLI finds the engine correctly. A status command
that answers a narrower question than its wording is worse than no status command, because it
converts "I have not checked" into "I have checked and it is fine".

**The builder cannot be set on a service at all.** The `Builder` enum is HEROKU, NIXPACKS,
PAKETO, RAILPACK — there is no `DOCKERFILE`. That explains a line in the morning's deploy log
that had been read as proof the file was working: a build initialises as RAILPACK and then flips to
`DOCKERFILE`. The enum holds the FALLBACK builder, and the Dockerfile is selected by a resolved
`dockerfilePath`. That field IS settable, so the migration became a hypothesis with a cheap test:
set it on the service, delete the file, deploy, and see which builder initialises.

The test was safe to run, and knowing why is the useful part: a wrong answer costs a failed build,
not uptime, because the platform keeps serving the existing deployment while a new one fails. The
result was unambiguous — INITIALIZING DOCKERFILE, with no Railpack flip, from an archive containing
no config file, and both replicas up.

**`numReplicas` is pinned in two places** — the top-level field and multiRegionConfig per region.
Setting only the top level leaves the region at its old count, which is the silent capacity halving
the migration existed to prevent. Both were set.

**The IaC path was abandoned, and the reason is not "it was hard".** The generator is good: it emits
per-region replicas and scopes itself with a named partial so the other thirteen services in the
project are untouched. But railway config plan cannot run. It wants an npm dependency tree at the
root of a C++23 repository whose deploy tars the working directory, and the SDK then rejects CLI
5.74.0 claiming it needs 5.42.1 or newer — because it locates the binary through `process.env._`, the
shell's last-argument variable, which is not the CLI. No authoring file was left behind: a config file
nothing reads is the defect being fixed, not a smaller version of it.

**`healthcheckInitialDelay` is lost.** It is not a service-instance field in any form.
`healthcheckTimeout` at 300 covers the model-load window, so this is stated rather than hidden.

The replica count now comes from the service through `scripts/railway_replicas.sh`, and a count that
cannot be read is reported as UNKNOWN rather than guessed. Both callers previously had a silent
default — 1 in the deploy script, 2 in the cutover check — and this gate has already told an operator
to expect 3 for four days after the count dropped to 2, which made an honest 4 lines read as a
shortfall. Verified in both directions: it prints 2 for the engine, and exits non-zero printing
nothing both for a service with no override and for a service that does not exist.

All four services now carry their own configuration. The queue nodes were migrated WITHOUT a deploy,
which is sound for a specific reason: service-instance config is inert until the next deployment, so
writing it changes nothing running, and when the file stops being read the containers do not re-read
anything. The risk only ever materialised on the next deploy after 2026-12-01, which now reads correct
settings. Their files were left in place; with the service holding the same values, the file going
unread is a no-op, and deleting it would require deploying a Raft cluster to prove nothing.

---

## 2. The paywall made signing up a sixtyfold downgrade

Asked how the mortgage assistant should be sold, the answer that came back was that the plan's phase
0 was backwards. It said "switch enforcement on". Enforcement on an unknown product converts zero
anonymous callers into zero paying ones.

The asymmetry settles the order. Serving calls nobody is making costs a rounding error on a replica
whose memory is paid for either way. Staying unknown costs the business.

**Then the numbers turned out to be worse than the argument.** A registered free account got 3
assistant replies A MONTH. A signed-out guest got 6 A DAY. Signing up made the product roughly sixty
times worse, at the exact step the site most needs a visitor to take. The inversion was known: a
comment recorded it as an accepted oddity. Recording an own goal is not the same as not scoring one.

The free allowance is now 240 a month: strictly below Pro's 250 so the tier ladder holds, and
strictly above a guest's 6-a-day month of 180, so signing up can never again be a downgrade. The
guest limit stays at 6 because it is keyed on a hashed client IP and raising it widens an abuse
surface without helping anyone who could simply register.

**A test that encoded the paywall was replaced, not deleted.** The suite asserted `pro > free * 20`,
which states a twenty-times paywall ratio rather than anything about correctness — it would have
blocked this change while being no evidence against it. It now asserts that free exceeds a guest's
month, and that Pro exceeds free. The ladder test is untouched.

The assistant is the only part of that site which is not a commodity. Fifty-one state guides and a
payment calculator are replaceable by a free package in an afternoon; an assistant answering a real
question against maintained per-state tax and insurance data is what somebody links to. Gating it was
gating distribution. The gate stays built and switched off, so turning it back on is a number rather
than a project.

---

## 3. A tag is not a version

Asked for Postgres 19 "because of too many CVEs", two measurements changed the answer.

**19.0 exists upstream, released 2026-11-19. Railway publishes no 19 image** — its template registry
carries 15, 16, 17 and 18 tags only, newest 18.7. Read from the registry's own tag list rather than
from documentation.

**And a major upgrade is not a tag change.** A 19 server refuses to start against an 18 data
directory. "Just redeploy with 19" is not an upgrade, it is an outage. This is the single most likely
mistake a reader of any upgrade plan will make, so it leads both plan documents.

**The CVEs were never an argument for 19.** Every advisory published 2026-11-12 was fixed in 18.7,
which upstream supports until 2030-11-14. The actionable change was therefore a MINOR one, and it was
urgent rather than routine for one specific reason: CVE-2026-120154 lets INSERT ... ON CONFLICT read
rows DESPITE ROW SECURITY, and row security through the `ofc_app` NOLOGIN role is exactly what
protects saved scenarios. Thirteen memory and type-confusion CVEs at CVSS 8.1 to 8.8 also applied, as
did an extension-script injection, because pgcrypto is loaded.

Both services were moved to 18.7 and the move was verified by DIGEST, not by status. This matters:
the redeploy of the mortgage database reported SUCCESS eleven seconds in, far too fast to have pulled
an image, on a platform already on record reporting SUCCESS while a container crash-loops. A changed
digest shows a new image ran. Resolving the `18.7` tag at the registry is what makes that digest mean
"18.7" rather than merely "different".

**The two databases shared a tag and ran different builds.** Both were pinned to `18`, and each had
resolved that mutable tag on its own deploy date, seventeen days apart. So the product database was
running an EARLIER 18.x than the mortgage one and was more exposed, not equally — and only the digest
showed it. Both are now pinned to the explicit minor, which changes nothing about which image runs
today and everything about which image runs next.

**"Use the official image" would have broken more than it fixed.** Railway's template is not a thin
wrapper: it generates a self-signed CA and server certificate at boot, and the engine connects with
`sslmode=require`, so vanilla Postgres would be REFUSED outright. It also runs tini as PID 1, refuses
to boot on a wrong mount path or an image-major/on-disk mismatch, holds a lock so two containers
cannot share a volume, ships pgvector and pgaudit, and pins collation with a reindex on drift. And
upstream's 19 image uses a PGDATA under the version number while the wrapper enforces one that is
not, so the swap would not even find the data.

The recommendation is to WAIT, in `docs/technical/POSTGRES_19_MIGRATION_PLAN.md`, reopening when
Railway publishes a 19 tag, lists 18 as a supported source major, upstream reaches 19.1, and the
prerequisites below are met. An external managed provider was assessed and rejected on its own
documentation, which says a major upgrade there requires a new project and a data migration — the
branch-and-swap premise was simply wrong, and it is recorded as rejected rather than dropped.

**Two contradictions in Railway's own documentation are reported rather than resolved**: the upgrade
guide lists source majors 14 through 17 while the HA page lists 14 through 18, and the PostgreSQL page
says no extensions are planned while the template ships two. Picking the convenient reading of a
contradiction is how a plan acquires a false premise.

---

## 4. A migrations system whose gate was required to fail first

Nine migrations existed as hand-applied files with no ledger and no runner, so nothing recorded what
had been applied anywhere and nothing would notice a schema edited by hand. One of them deletes rows,
so replaying the set is not a recovery plan.

What is new: an append-only ledger whose trigger refuses UPDATE and DELETE in the database rather than
by convention, a transactional advisory-locked runner that refuses edited or out-of-order migrations,
and `scripts/check_schema_drift.sh`, which builds the schema the ledger implies on a throwaway
container and diffs it against the live catalog — comparing RLS flags, policies, ROLES and their
attributes, grants and event triggers, because this product's security posture lives in those objects
and a gate reading only tables would call a dropped policy "no drift".

**The positive control found two bugs in the gate**, which is the entire argument for demanding one.
A `UNION` typed a detail column as `name`, silently truncating every row at 63 bytes — long enough to
look right, short enough to hide a role's `bypassrls` flag, so a security-relevant drift was invisible
to the security gate. And roles are CLUSTER-level rather than per-database, so role drift appeared on
both sides of the comparison and cancelled itself out.

The self-test now goes RED for ten deliberate drifts and GREEN after each repair, and asserts three
further refusals. It was re-run independently before being believed, and it left no container behind.

**The baseline is provisional and must not be trusted yet.** It was captured by replaying the legacy
files into a scratch database, so it is the schema AS DESIGNED, not as production is. If production
has drifted, the gate would enforce the wrong shape confidently, which is worse than not enforcing it.

**No restore has ever been verified.** The existing NAS backup script runs `pg_restore --list`, which
reads a table of contents and proves an archive is parseable — not that it restores. Until a restore
is rehearsed, the backups are an assumption. This is the most actionable finding of the evening.

---

## 5. Two failures that were the instrument, not the system

A sweep of the embeds' search-indexing posture reported two failures: page similarity over its ceiling
on the state guides, and — more alarmingly — a FAILED POSITIVE CONTROL in the advertising suite, the
check that at least one route renders a unit.

The second one mattered more than the first. If no ad unit can render anywhere, then "no ads on the
embeds" passes VACUOUSLY, and the whole advertising conclusion rests on nothing.

Both suites had been run against a development server. Re-run against production, where this suite has
always been verified, both are fully green, control included. A development server with no ad slot
configuration produces exactly that pair of symptoms.

So the advertising conclusion was re-established on evidence that does not depend on whether ads can
render at all: the allowlist is fail-closed and normalises double and trailing slashes before
matching, a source-level test forbids ad identifiers in the embed routes, and the served HTML of every
embed page contains no ad script. Four independent checks rather than one green suite.

---

## 6. Claims that went stale inside the same evening

Three of them were ours, and none got an exemption for it.

**An advertised capacity figure had been fifty per cent too high for three months.** A feature
document stated a site-wide ceiling computed from three replicas. The count dropped to 2 on
2026-08-21. The document a reader consults for capacity had been overstating it ever since, and the
sweep for a different stale claim is what found it.

**A "now" inside a dated note is still a present-tense claim.** A dated deploy note said the replica
count "is now derived from" the config file. The sentence is dated, but "now" points at today, so it
read as current and was wrong.

**Three line-range citations were broken by the same evening's own edit.** Removing one line from the
deploy script and rewriting the replica derivation shifted every range after it, so the code map
pointed at the wrong code. This is the second day running that editing a file broke a citation into
it. The ranges are gone; the descriptive labels above them already anchor a reader, and a label does
not move when a line is inserted. Citations into a file whose line count did NOT change were left
alone — an accurate citation is not improved by deleting it.

**And the upgrade plan written at the start of the evening said "Plan only. Nothing here has been
executed"** while proposing the minor upgrade as the pending action. It was executed about an hour
later. It now carries a status note with the digest evidence, while its dated measurements are left
exactly as taken, because a dated measurement rewritten to match today stops being a measurement.

**Two sitemap dates were stale because a commit edited route files without re-running the lastmod
gate** — twice in one evening, by the same hand. A sitemap advertising a two-month-old date for a page
changed today is a small lie told to a crawler.

---

## 7. The embeds, which were already a distribution channel nobody was using

Four iframe widgets had been live and unpromoted. An iframe needs no API key, which is exactly why it
is the right channel to lead with.

**The attribution link was relative.** Inside an iframe on somebody else's domain, a relative href
resolves against THEIR origin — so every embed ever placed on a third-party site had been linking to
that site's own root. The one thing the embeds existed to earn was going to the host. It is absolute
now, and governed by the same single condition as white-label suppression, so a paying customer
cannot see branding and a free embed cannot lose it.

**An empty numeric parameter rendered a zero per cent mortgage.** Because `Number("")` is 0, a URL
ending `rate=` produced a confident, plausible monthly payment at no interest, on a stranger's
website, with nothing to signal failure — strictly less safe than an error. The guard now tests
whether the STRING has content rather than asking `Number()` its opinion of emptiness, while an
explicit zero still passes, because a caller legitimately means it.

**The widgets told search engines two contradictory things**: each declared `noindex` AND a canonical
pointing at a different page. Those are opposite instructions. The mortgage widget's canonical named
the site ROOT, so a bare iframe was nominating itself as the homepage's duplicate. The canonicals are
gone and `noindex` stays; `nofollow` was deliberately NOT added, because the attribution link is the
point.

**The embeds' security policy permitted ad and payment script on customers' pages.** Nothing loaded
it, so this was capability rather than a leak, but an embed runs inside other people's websites. It is
narrowed, with a positive control proving ordinary routes still permit those hosts — otherwise
"no ad hosts here" would be indistinguishable from having broken advertising site-wide. Verified in
production on both sides.

Left deliberately: the embeds still permit analytics hosts, because the root layout requests a tag
script on every route and removing the hosts from the policy without first gating the request would
print violations in customers' consoles. Named plainly because it has a consent dimension for whoever
embeds the widget: until the request is gated at source, an embed loads our analytics inside a third
party's page.

---

## Still open

- **Verify the email feature end to end.** Deferred deliberately: proving it means either sending real
  mail or reading live credentials out of tracked dotenv files, and neither should happen casually.
- **Make the migration baseline real.** It needs a reachable product database — that service has no
  TCP proxy, remote shell is refused, and the credential loader does not permit a database client as
  its child. Until then the baseline is the scratch replay and the gate is enforcing a design.
- **Rehearse a restore.** The plan composes one from the pieces that landed tonight; it has never
  been run. An unverified backup is not a backup.
- **Gate the analytics request on embed routes at source**, then narrow the policy further.
- **Switch metering on for the finance API** — count callers, refuse nothing. There may already be
  callers and we cannot name one, and no pricing decision can be made without that number.
