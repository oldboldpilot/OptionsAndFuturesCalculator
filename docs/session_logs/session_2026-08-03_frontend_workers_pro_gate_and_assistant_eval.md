# Session 2026-08-03 — frontend served by Workers not Pages, Pro gate verified, assistant eval corrected

@author Olumuyiwa Oluwasanmi

Three findings, each of which began as "verify the thing is deployed" and ended
in a measurement that had been wrong for some time. Recorded with the wrong
predictions kept next to what replaced them, per `config/update_policy.txt`.

---

## 1. The frontend has not been deploying — `wrangler pages deploy` hits nothing

**Asked:** confirm the Pro-gate UI is deployed.

**Found:** the live domains are **Workers custom domains** bound to the
`optionsandfuturescalculator` Worker. The Pages project still exists and still
accepts deployments, so `wrangler pages deploy` succeeded and printed a URL every
time while changing nothing a user could see. Commit `9b81acf` had moved the site
to a Workers static-assets deployment (`frontend/wrangler.toml`, `[assets]
directory = "./out"`); CLAUDE.md and `config/update_policy.txt` were never
updated, so the documented command kept deploying to an orphan.

That is the five "completed" frontend deploys.

**Impact was not cosmetic.** The bundle the apex actually served had
`NEXT_PUBLIC_SUPABASE_URL` set to `http://localhost:8000` with an
`eyJyb2xlIjoiYW5vbiJ9.placeholder` anon key. Sign-in could not work on the real
domain, so the **JWT route to Pro was dead in production**. The licence-paste
route still worked, which is why the gate looked healthy whenever it was tested
with a licence.

### Why it resisted diagnosis

Every cheap signal pointed away:

| Signal | Read as | Actually |
| --- | --- | --- |
| apex returns 200, plausible HTML | site fine | stale bundle |
| all 11 chunks resolve 200 | no chunk-404 bug | true, and irrelevant |
| `cf-cache-status: HIT` vs origin `max-age=0, must-revalidate` | caching bug ⇒ purge | no origin behind the hostname at all |
| purge changed nothing | purge did not propagate | nothing to revalidate against |

Two signals do discriminate, and both are now written down:

1. The apex carried **none** of the Pages response headers `pages.dev` returns
   (`access-control-allow-origin`, `referrer-policy`, `x-content-type-options`).
2. The apex/`www` DNS records are `AAAA 100::` — the discard prefix Cloudflare
   writes for a **Workers custom domain** — not a CNAME to `pages.dev` as
   CLAUDE.md claimed. Settled with `GET /accounts/{acct}/workers/domains`.

### A wrong turn, kept on the record

Acting on the stale CLAUDE.md, the apex and `www` were attached to the **Pages
project**. While a Workers custom domain owns a hostname, that attachment sits at
`status=pending` indefinitely and does nothing. Both were removed; the Worker
never lost the domains, so the site was not disrupted. Do not repeat this.

### Fix

`cd frontend && npx wrangler deploy`. Verified after:

- apex and `www` both serve the current build (chunk `3xsnu_mlycfsr`)
- **32/32 routes 200**, 12/12 referenced assets 200
- Pro-gate UI present in the served bundle
- zero chunks referencing `localhost`; real Supabase and API origins present
- Supabase auth origin returns 200, so sign-in has a working backend

---

## 2. Pro gate — verified for the first time against the deployed engine

`scripts/probe_pro_gate.sh` drives `smoke_client` over **native gRPC**, which does
not survive the Railway ingress. Pointed at production it cannot connect — and
because it collapses every non-zero `smoke_client` exit to "deny", an endpoint
that was never listening rendered as **10 PASS / 2 FAIL**. Ten rows passed against
nothing. Against the Railway TCP proxy it failed identically: that proxy forwards
to `RAILWAY_TCP_APPLICATION_PORT=50052` while the engine serves 50051.

So the gate had never actually been verified against the deployed engine.

`scripts/probe_pro_gate_web.py` (new) speaks gRPC-Web, the only transport that
reaches the container, and fixes the two properties that made the old run
unfalsifiable:

- A refusal counts only when the engine says so. `grpc-status 7` is a deny; a
  transport error, a 5xx or status 14 is scored **ERROR**, never deny. Status 8 is
  called out separately so a quota refusal is not misread as the gate.
- A **liveness control** runs first — an anonymous single-leg call the free tier
  must allow. If it does not come back allowed the run aborts as INVALID rather
  than printing a column of passes an unreachable engine would earn.

Credential minting moved to `scripts/mint_pro_gate_creds.mjs` so both transports
assert against identical credentials, and licences keep going through the billing
worker's real `mintLicence` — the signed body is `{s,t,e,v}`, and a probe that
invents its own shape only tests its guess.

**Result against `api.optionsandfuturescalculator.com`: 13/13, control live.** Pro
allowed via both JWT and licence; free, browser-writable `user_metadata`,
post-signature payload edits, expiry, `alg:none`, wrong-key and garbage all deny.

One failure was the probe's, not the engine's: the first run sent a `licence`
header. A licence rides the **same channel as an API key** (`api_key.cpp`'s
`kLicencePrefix` branch on `x-api-key`); the invented header was silently ignored
and the caller scored anonymous, which reads exactly like a broken gate.

---

## 3. The assistant regression did not exist — it was measured on the wrong engine

Production serves the assistant **in-process on sensen** (`backend=sensen`,
confirmed in the live startup line). The holdout had been measured with
`llama-cli`, an engine that never handles a request.

| Model | Holdout | Emitted `<params>` | Baselines |
| --- | --- | --- | --- |
| Deployed (QLoRA, 4 epochs) | **13/16** | 11/16 | both pass |
| Retrain (QLoRA, 2 epochs) | 5/16 | 3/16 | both fail |

The earlier **7/16** for the deployed model came from `llama-cli` and is void. The
2-epoch retrain, commissioned to fix that phantom, is materially worse and was
**not deployed**. This also refutes the working theory that 4 epochs had overfit —
halving them made it worse.

sensen is exonerated: the deployed model emits exact, correct params through it.

### Two of my own errors that produced confident wrong conclusions

**Stale engines.** Five `calculator_engine` processes were all bound to `:50051`
under `SO_REUSEPORT`, each with a different model, kernel load-balancing requests
across them. That produced a SPY iron-condor prompt answered with bond-futures
text, initially reported as KV-cache bleed in the serving path. It was not — the
request had landed on a different engine. Cleanup never worked because `comm` is
truncated to 15 chars, so `pkill -x calculator_engine` matched nothing; and
`pkill -f` on the binary path kept killing the launching shell (exit 144).

**Market data.** Symbol verification needs live Alpaca credentials. Without them
the RPC refuses or clarifies regardless of the model, so both a good and a bad
model scored an identical **3/16** — a measurement of the verification layer. The
valid signal is `[assistant] raw model output`, logged before verification runs.

Both are written up in `docs/guides/ASSISTANT_EVALUATION.md` with the assertions
that catch them.

### Dataset correction — wrong twice, including the file itself

An earlier "31.1% clarification, roughly double the recipe" figure counted every
multi-turn row, and `make_modification` is also four-turn. Correcting that gave
16.5% — but that was measured on `agent/dataset/out/` (11,400 rows), while
training consumed `agent/dataset/data/` (**28,500 rows**, `--data ../dataset/data`).
A second wrong-artifact error, caught only while preparing this commit.

The set actually trained on: clarification **18.5%**, modification 15.3%,
single-turn 66.2%, 47 strategies with min 546 / median 605 / max 657 rows. Well
balanced. The conclusion is unchanged — the dataset was never the problem — but
the numbers previously recorded described a file the model never saw.

### 16/16, and not one line of it was the model

The remaining failures all turned out to be ENGINE defects. The deployed model
was never changed; three fixes took the holdout from 13/16 to **16/16** at the
RPC layer. Each had been sitting behind a plausible-sounding comment.

**1. Bare futures directives were refused.** The model reads "Long NQ, 45 days,
2 contracts." as a request to PLACE a trade and answers in prose, emitting no
params. `recover_bare_futures_directive` parses exactly that shape
deterministically and feeds the result through the SAME validation and GP-ARA
gate the model's own output faces -- a recovery, not an override, so it can turn
a wrong refusal into a correct answer but never a refusal into a wrong answer. It
returns nullopt on any partial match; 15 tests pin that, 11 of them negative
controls (options strategies, share purchases, missing quantity, contradictory
direction, unknown roots).

**2. An unclosed `<think>` swallowed a correct answer.** `strip_think_block`
dropped everything when `</think>` was missing, documented as "the model hit the
token ceiling mid-reasoning ... leaves nothing to interpret". The real output is
`<think>\n\n<params>{...}</params>` -- 130 bytes, `</params>` balanced, simply
missing the closing think tag. Not truncation: a complete answer being discarded
as unparseable. That alone was failing the `bull call spread on NVDA` baseline in
production while the model had gotten it right.

**3. The five two-expiry strategies were unreachable.** `StrategyParams` carried
one `expiration_days`, so the verifier refused calendar, diagonal, double
diagonal, PMCC and futures calendar outright. The comment argued a single field
"can never resolve that" -- sound about the FIELD, wrong about the PRODUCT: the
calculator prices all five and its UI already completes the far leg from a second
chain (`StrategySelector.tsx`). A trader asking "calendar spread on CL, 45 days"
was refused for asking about a strategy this calculator sells. Added optional
`far_expiration_days`; a near leg alone is accepted, and what is still refused is
a far leg at or before the near leg, which no second chain can repair.

I initially called these three rows a holdout-vs-design disagreement and said the
holdout was wrong. That was the wrong call, and the correction came from being
told to fix it rather than excuse it. The schema was the defect.

Fix 3 also revived the ambiguity inference for those strategies: `crude oil
calendar spread, 60 days` now resolves its `CL` root from the Futures category
plus lexical support. That path had always existed but the multi-expiry refusal
fired first, so it was dead for all five.

**Automated reasoner:** the cross-field rules live in
`AssistantParamsDomain::translate()`, which is the GP-ARA domain itself, so the
reasoner is updated by construction -- `RuleBasedReasoner` consumes the resulting
`VerificationFacts` verdict and there is no separate SMT encoding to keep in
sync. Its banner's own worked example of a contradiction ("a calendar spread
asked to live inside a single expiration_days field") was exactly the rule that
changed, and now describes the ordering rule that replaced it.

Verification tests: 92 -> **111 checks, 0 failed**.

---

## Tri-agent review (config/agents/code_update_agent.yaml)

`agy` reviewed the full staged diff and returned 4 MUST-FIX. `cursor-agent` was
unavailable (`Authentication required`, exit 1), matching this repo's documented
precedent for that tool; the Claude leg was performed inline. Per the 2-of-3
convention the review stands on agy plus self-review.

Applied:

1. `agent/train/eval_grpc.py` was missing the mandatory
   `@author Olumuyiwa Oluwasanmi` tag. Added. (`defect_holdout.jsonl` is JSONL and
   cannot carry a header comment — not applicable.)
2. `scripts/eval_assistant_sensen.py` hand-rolled its argument parsing and had
   three real failure modes: `--layer` with no value raised `IndexError`; a value
   equal to the layer name stripped a positional path; and a stray value shifted
   the positionals. Replaced with `argparse`; all three now produce a clean error.
3. Same script: a short `gots` list let `zip()` end the run early, so unmatched
   rows vanished from the score instead of failing. Now padded with `None`, and
   the length mismatch warns explicitly. This is the exact class of bug this whole
   session was about — a measurement that quietly reports fewer rows than it was
   asked to check.
4. The results table read as if `13/16 correct` and `11/16 emitted` were
   inconsistent. They are not: 2 rows correctly emit no parameters because asking
   is the pass condition (11 emitted + 2 asked + 3 wrongly silent = 16). Spelled
   out in the guide.

Also applied from SHOULD-FIX: `single_engine_or_die` now skips the local process
and socket check for a remote target instead of falsely aborting, and derives the
port from the target rather than hardcoding 50051; the engine-log offset is taken
as a byte offset rather than a character count, which would drift under
`errors="replace"` decoding.

Not applied: agy noted the diff contains no `frontend/` changes to substantiate
the Workers deployment claim. Correct observation — `frontend/wrangler.toml` was
added in `9b81acf` and is unchanged here; the claim is substantiated by the live
verification recorded above, not by this diff.

## Housekeeping

- Two commits made earlier in this session carried AI co-author and session-link
  trailers, which the authorship policy in `config/update_policy.txt` prohibits.
  History was rewritten with `git filter-branch --msg-filter` to strip them; sole
  author is Olumuyiwa Oluwasanmi, verified with a grep over the rewritten range.
  (First attempt used a sed that also squeezed blank lines; its `N` pulled the
  trailer into the pattern space before the delete rule could match, so the
  session-link line went but the co-author line survived. A plain `grep -v` filter
  worked.)
- `config/update_policy.txt` and CLAUDE.md both documented the Pages deploy
  command and a CNAME that no longer exist. Both corrected.
