# Selling the finance API

How the engine behind api.optionsandfuturescalculator.com gets sold to banks, mortgage companies
and real-estate professionals. Written 2026-11-24 against the surface that exists on that date,
with every claim about the product measured rather than recalled; the measurements are quoted so a
later reader can tell which ones have rotted.

Its companions: `docs/BUSINESS_API.md` is how a customer is onboarded once they say yes,
`docs/FINANCE_API.md` is the reference they are handed, and `docs/api/GRPC_SURFACE.md` enumerates
the wire surface. This file is the only one about getting them to say yes.

---

## 1. The thing to understand before anything else

**The arithmetic is not the moat.** Any mortgage company can get amortization from a free npm
package in an afternoon, and most already have it somewhere. A plan that sells "we compute
payments" is selling a commodity against free.

What cannot be casually rebuilt is the **maintained reference data and the single source of truth
for rates**:

- `GetStateAssumptions` serves per-state property tax, homeowners insurance and effective tax
  rates for all 51 jurisdictions, with provenance. A brokerage that wants this builds a research
  function, not a feature, and then owns it forever.
- Every survey rate the system prints comes from one backend owner reading official sources, and
  is cited at the point of use. That was built on 2026-11-24 precisely because scattered copies
  had drifted — the fix is now the product's differentiator.
- `ComputeRentVsBuyBatch` answers all 50 states in one RPC, and `ComputeAmortizationBatch` does a
  loan book in one. The batch shape is what turns a calculator into a portfolio tool.
- Money is exact. The engine computes in `BigDecimal` at thirty-eight places and returns money as
  a **string**, so nothing rounds on the way out. A float64 pipeline drifts over 360 periods; this
  one does not, and that is an argument a compliance officer understands.

So: **lead with the data and the exactness, let the 48 RPCs be the proof of depth.** The pitch is
"the mortgage math layer you do not have to maintain", not "an amortization endpoint".

---

## 2. The one thing standing between this and a first invoice

Measured on production 2026-11-24, 17:5x PST, against
`/sensen.finance.Finance/ComputePayment`:

| request | result |
| --- | --- |
| valid body, **no** key | `HTTP=200`, correct answer |
| valid body, **forged** `sk_live_` key | `HTTP=200`, correct answer |

**Enforcement is off in production.** The machinery is all built — `issue-key` mints secret and
publishable keys, stores only a SHA-512 digest, binds publishable keys to an origin allowlist, and
the two-axis quota meters requests per minute and compute units per hour with an honest
retry-after — but the engine is not configured to require any of it. A forged key is not refused,
which means there is no enforcement to bypass rather than a lenient policy.

Nothing else in this plan matters until that changes. A paid tier is meaningless while the free
tier is unlimited and anonymous, and worse, there is **no usage data**: there may already be
callers, and we cannot name one or count them. The first act of this plan is not outreach, it is
switching on the gate and watching who turns up.

`docs/BUSINESS_API.md` documents the `401` that an unkeyed call *would* receive. That is accurate
about the enforcing posture and silent about the fact that production is not in it — the same
defect shape this repository keeps finding: a statement true when written, left standing while the
world moved.

---

## 3. What is already built, and what is missing

Being explicit about this is the difference between a plan and a wish. Measured 2026-11-24:

**Built and live**
- 48 RPCs on `sensen.finance.Finance`: time value of money, amortization and its batch,
  `ComputeHeloc`, `ComputeRefinance`, `ComputePayoffTiming`, `ComputeMortgageRecast`, NPV/IRR/XIRR,
  depreciation, bonds and treasury bills, futures and margin, `ComputeRentalRoi`,
  `ComputeRentalCashFlow`, `ComputeHomeFutureValue`, `ComputeRentVsBuy` and its batch,
  `ComputeHomeNpv`, `ComputeClosingCosts`, `ExplainMortgage`, `GetStateAssumptions`, option
  pricing and portfolio statistics.
- Two transports: JSON over the public domain with no codegen, and native gRPC through the TCP
  proxy for throughput.
- Key issuance with secret/publishable split, origin binding, digest-at-rest.
- Per-key and per-tier quotas on two axes, enforced at the call site where the request is
  deserialized and can therefore be priced by the work it asks for.
- A production consumer that is not us-in-a-demo: mortgagefvcalculator.com, 1,247 indexed URLs,
  calling `ComputeRentVsBuy`, `ComputeRentVsBuyBatch` and `ComputePayment`, with wire-contract
  tests pinned against the live endpoint.
- 1,457 lines of API reference and 227 lines of onboarding runbook.

**Missing — and each one is a deal-blocker for a different buyer**
1. **Enforcement off** (§2). Blocks everyone.
2. **Issuance is manual**: mint on the CLI, paste into `FINANCE_API_KEYS` on the service. Fine for
   the first ten customers by hand — genuinely *better* than self-serve for learning what to
   charge — but it cannot carry a hundred.
3. **No billing path** for an API tier. Nothing turns a key into an invoice.
4. **Docs are internal-facing.** There is a reference, but no public quickstart a developer can
   reach without being handed a file, and no page that ranks in a search.
5. **No uptime statement and no status page.** A bank's vendor review asks for both. Absence does
   not lose the deal at the pitch, it loses it at procurement.
6. **No reference customer willing to be named.** The strongest asset is a live site we own, which
   a buyer will discount as self-dealing until a third party is on the list.

---

## 4. Segments, and the RPCs that carry each one

### Banks and credit unions — digital channel and mortgage operations
- **Pain**: the calculator on the public site disagrees with the disclosure the LOS produces,
  usually by pennies, and someone has to explain why. Float arithmetic and a stale rate are the
  usual causes, and both are ours to remove.
- **Lead with**: exact decimal money, one rate source with a citation, the same engine behind the
  public widget and any back-office batch.
- **RPCs**: `ComputePayment`, `ComputeAmortization`, `ComputeDetailedAmortization`,
  `ComputeRefinance`, `ComputeHeloc`, `ComputePayoffTiming`, `ComputeMortgageRecast`,
  `ComputeClosingCosts`.
- **Buying trigger**: a reconciliation complaint, a compliance finding, or a website rebuild.
- **Who signs**: digital channel manager at a credit union; at a bank, a product owner with a
  vendor-review gauntlet behind them. Credit unions are the better first target — shorter chain,
  real budget, and they buy from small vendors routinely.

### Mortgage companies, IMBs and loan officers
- **Pain**: lead generation needs calculators that convert, and refi opportunity has to be scanned
  across a whole servicing book, not one loan at a time.
- **Lead with**: the **batch** RPCs. One call for the book, one call for all 50 states. Then
  `ExplainMortgage`, which turns a number into a sentence a borrower reads — the thing an LO
  actually forwards.
- **RPCs**: `ComputeRefinance`, `ComputeAmortizationBatch`, `ComputeRentVsBuy`,
  `ComputeRentVsBuyBatch`, `ComputeClosingCosts`, `ExplainMortgage`.
- **Buying trigger**: a rate move. When rates fall, every IMB wants to know which of its loans are
  refinanceable this week; that is a batch call and they will pay for it that month.
- **Who signs**: VP of marketing or the in-house dev lead. Fastest cycle of the three segments.

### Real-estate professionals, brokerages and proptech
- **Pain**: investor clients ask "what does this property actually return", and the answer is a
  spreadsheet someone built once that nobody trusts.
- **Lead with**: `GetStateAssumptions`. The per-state tax and insurance data is the part they
  cannot get anywhere for a price they would pay, and it is what makes a cash-flow number
  defensible rather than a guess.
- **RPCs**: `ComputeRentalRoi`, `ComputeRentalCashFlow`, `ComputeHomeFutureValue`,
  `ComputeHomeNpv`, `ComputeRentVsBuy`, `GetStateAssumptions`.
- **Buying trigger**: a brokerage building an investor-facing portal, or an agent's marketing
  spend looking for a tool.
- **Who signs**: brokerage tech lead, or the proptech founder directly. Smallest cheques, shortest
  sales cycle, most willing to try something on a Tuesday.

### Fourth segment, not in the original brief but cheapest to serve: developers
Publishable keys bound to an origin allowlist make this a **drop-in browser widget** business as
well as a server-side API. Those origin-bound publishable keys are THIS engine's
(`backend/src/issue_key.cpp` requires at least one origin, and the registry refuses a publishable key
that has none); mortgagefvcalculator.com's own public API is a different mechanism with secret
bearer keys and no origin binding, and conflating the two wasted an afternoon once. A proptech
developer who can paste a `pk_live_` key into a page and
have a working calculator in ten minutes is a customer acquired with zero conversation. This is
the segment the documentation strategy in §6 serves, and the one that fills the funnel for the
other three.

---

## 5. Pricing

Anchor it to the **two-axis quota already implemented**, because that is both honest and a
differentiator: competitors meter requests, which charges the same for a closed-form payment and a
million-path Monte Carlo that costs six orders of magnitude more.

| tier | limits | transport | who it is for |
| --- | --- | --- | --- |
| Developer | low rpm, low CU/hr, no uptime commitment | JSON, publishable or secret | evaluation, side projects, widgets |
| Business | per-key rpm and CU/hr, written on the key | JSON | IMBs, brokerages, proptech |
| Enterprise | custom limits, named endpoint | native gRPC via the TCP proxy | banks, credit unions, volume batch |

**The figures below are anchors to be tested, not findings.** No willingness-to-pay research
exists yet, and inventing a number and then defending it is how a plan becomes fiction.

- Developer: free. Its job is adoption and a support-free path to a working integration.
- Business: start the ask at **$199/month**. Rationale: a bank or IMB's alternative is roughly a
  developer-week to build amortization plus an indefinite commitment to maintain state tax and
  insurance data — call it $10–15k fully loaded in year one. $199 also sits under the threshold
  where most departmental buyers must involve procurement, which is exactly where the first ten
  deals should live.
- Enterprise: start at **$1,500/month**. What is actually being bought at that price is the
  uptime commitment, the gRPC endpoint and a named human — so do not offer the tier until §3's
  items 5 and 6 exist, or the first renewal conversation is lost.

**How to validate rather than guess**: concierge-onboard the first five customers by hand, quote
each one a different number inside a band, and record what they accept and what they push back on.
Five data points from closed deals beat any amount of desk research, and the manual issuance path
in §3 item 2 makes this the path of least resistance anyway.

---

## 6. Channels, ordered by cost per qualified conversation

1. **The site we already own.** mortgagefvcalculator.com has 1,247 indexed pages drawing people
   who are, by definition, looking for mortgage math. A developer page and a footer line naming the
   API converts a fraction of that existing traffic for the price of writing it. Cheapest channel
   available and it is already paid for.
2. **Documentation as SEO.** The same programmatic-content machinery that built 1,247 ranking pages
   points at "mortgage amortization API", "rent vs buy API", "closing cost API", "property tax by
   state API". This is the single highest-leverage play here because the capability is already
   built, measured and gated — it is the one channel where this project has a demonstrated unfair
   advantage.
3. **API marketplace listing.** A RapidAPI-style listing is a weekend of work and puts the surface
   in front of developers already shopping. Treat it as discovery, not as the billing system.
4. **Direct outreach to credit unions and IMBs.** Reachable without a sales team: America's Credit
   Unions' member directory, NMLS Consumer Access for IMBs by state, and state MBA chapter member
   lists. The message is one screenshot of their own calculator disagreeing with ours by a penny,
   which is a cold email that gets answered.
5. **Partnership with POS and LOS vendors.** They need calculators and do not want to maintain
   state tax data either. Longest cycle, largest contracts, worth starting early precisely because
   it is slow.
6. **Investor-facing real-estate communities.** Developers and analysts building their own tools
   are a credible early-adopter pool. Earn the attention with something useful rather than a pitch.

---

## 7. Phases, each ending in a measurable gate

**This ordering was reversed on 2026-11-24, and the reason is worth keeping.** Phase 0 used to be
"switch enforcement on". That is the wrong first move, because enforcement on an unknown product
converts zero anonymous callers into zero paying ones. The asymmetry decides it: the cost of serving
calls nobody is making is a rounding error on a replica whose memory is paid for either way, and the
cost of staying unknown is the whole business. Enforcement is now phase 2, after there is something
to enforce against.

**Phase 0 — become known.** Three moves, none of which needs a gate.

- *Take the assistant out from behind the paywall.* Done 2026-11-24: a registered free account went
  from 3 replies a month to 240. That also ended an inversion in which signing up cut a visitor's
  allowance sixtyfold, because a guest got 6 a day. The assistant is the only part of the mortgage
  site that is not a commodity — fifty-one state guides and a payment calculator are replaceable by
  a free package in an afternoon — so gating it was gating distribution.
- *Meter, do not gate.* Record what is called and how often, and refuse nothing. That produces the
  usage figures a buyer conversation needs and that no amount of planning substitutes for: there may
  already be callers today and we cannot name one. State the limit honestly rather than papering
  over it — attribution is only as good as the identity a caller presents, so unkeyed traffic can be
  counted but not identified.
- *Promote the embeds that already exist.* `/embed/mortgage`, `/embed/heloc`, `/embed/rental-roi` and
  `/embed/biweekly` have been live and unpromoted. Each now carries an absolute attribution link
  governed by the same single condition as white-label suppression, so a Pro customer still gets
  unbranded output while every other embed on someone else's site is a backlink.
*Gate*: one embed or caller belonging to somebody we did not create, found without paying for it,
and a non-zero organic trend on the pages carrying the embeds.

**Phase 1 — make it findable.** A public quickstart that takes a developer from nothing to a
correct `ComputePayment` response, a developer page on the existing site, and the four documentation
pages aimed at the search terms in §6 item 2.
*Gate*: the quickstart works when followed literally by someone with no access to this repository,
and the pages are in the sitemap and indexable — the same `indexability` check the rest of the
site already passes.

**Phase 2 — make it sellable.** Switch enforcement on, with the existing site's keys issued first
so nothing we own breaks. Phase 0's metering is what makes this safe: by now the callers are known,
so turning the gate on is a decision with evidence behind it rather than a guess about who breaks.
*Gate*: an unkeyed call returns `401` and a forged key returns `401` on production, verified by the
same two probes that produced §2's table; mortgagefvcalculator.com still renders every family; and
the number of distinct callers that appear is written down.

**Phase 3 — first five customers, by hand.** Concierge onboarding, deliberately. Vary the price
inside the band and record the answers.
*Gate*: five issued keys belonging to organizations we did not create, with usage, and a written
record of what each accepted. One named reference closes §3 item 6.

**Phase 4 — remove the manual steps that are now hurting.** Self-serve issuance and billing, built
against what the first five actually needed rather than what seemed likely. An uptime statement and
a status page, which Enterprise cannot be sold without.
*Gate*: a customer goes from landing page to a working key and a charge without a human.

The ordering is deliberate: every phase before 4 is cheap, and phase 4 is the expensive one. Doing
it first would mean building self-serve billing for a price point nobody has yet agreed to pay.

---

## 8. What this plan does not claim

- **No market sizing.** No TAM figure appears here because none has been measured, and a fabricated
  one would get quoted back later as though it had been.
- **No conversion or revenue forecast.** With zero customers and enforcement off, there is no base
  rate to forecast from.
- **No competitive teardown.** Competitors have not been examined; §1's claim about free npm
  packages is an argument about what buyers can trivially substitute, not a survey.
- **The $199 and $1,500 are anchors**, labelled as such in §5, with the method for replacing them
  with evidence.
- **"48 RPCs" counts the service's declared methods** in the proto, not endpoints under contract or
  under test. Depth of coverage per RPC varies and has not been audited for this document.
