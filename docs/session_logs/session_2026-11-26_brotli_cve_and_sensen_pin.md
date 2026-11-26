# 2026-11-26 — Brotli CVE-2026-111497, and a 280-commit sensen pin verified but not moved

## Brotli (shipped)

CVE-2026-111497 is an out-of-bounds write in the Brotli decoder, Ubuntu priority high
(USN-8956-1/-2, USN-8958-1), fixed upstream in brotli 1.0.10/1.1.1/1.2.1. The full reasoning and
every measurement is in docs/ENGINEERING_NOTES.md under "Brotli CVE-2026-111497: three surfaces,
and only one of them apt can fix". What changed in the tree:

- A version-FLOOR gate after every apt install that brings in a brotli package —
  backend/Dockerfile (builder and runtime), backend/Dockerfile.queue-node (builder and runtime),
  deploy/gateway/Dockerfile. Floor `1.1.0-2ubuntu0.1`. It counts the packages it compared, so it
  cannot pass vacuously.
- Two comments corrected where measurement contradicted them: backend/envoy.yaml claimed the Envoy
  binary links libbrotlidec and that libbrotli1 is needed for Envoy to load (both false — Envoy
  carries its own brotli statically and the upstream image has no libbrotli package at all), and
  backend/CMakeLists.txt still said Ubuntu 24.04 ships only the affected 1.1.0.

Controls, all run rather than reasoned about:

| control | result |
| --- | --- |
| gate with a patched `libbrotli1` | build rc=0, "1 package(s) at or above 1.1.0-2ubuntu0.1" |
| gate with no brotli package | FATAL "would pass vacuously" |
| gate with `libbrotli1=1.1.0-2build2` forced | FATAL naming the CVE and both versions |
| brotli decompressor accepted by envoy v1.33 | `configuration OK` |
| a real-but-unregistered type, same validation | "Didn't find a registered implementation" |
| this repository's envoy.yaml on v1.33/v1.36/v1.39/v1.40 | `OK` on all four |
| `ldd` on `calculator_engine` and `smoke_client` | no libbrotli in either NEEDED closure |

Open, and deliberately not done here: moving the Envoy pin off 1.33 (a branch with no fixed
release) to 1.36.12+/1.39.3/1.40.0. The config validates unchanged on all of them, so the edit is
one line; the verification is a deploy, which is why it is a separate decision.

## sensen pin (verified, NOT moved)

New sensen master `26d90bb36` is FAIL: 19 on its own slim-closure checker (the growth history is
4 → 7 → 13 → 19), so the current pin `2b842edd6` remains the newest green commit. The 19 are two
events: an llm_pipeline implementation consolidation, and seven translation units added to the
source tree and never registered in the slim group registry. A +10/−2 registry fix takes the
checker to OK, and with it the whole bump verifies CPU-only in this superproject:
scripts/sensen_module_closure.py --check clean at 163 modules / 212 files (was 204), the full
engine built in 5504 steps with 0 failures, and `ctest` 217/217 passing.

Two defects surfaced on the way, neither in this repository's modules:

- A real regression in an SGEE test, found as SIGILL with no output. The faulting instruction was
  `ud2` — a compiler trap, not an unsupported instruction, which is what redirected the diagnosis.
  The new sensen enables _LIBCPP_HARDENING_MODE_FAST build-wide, which turns the precondition of
  `std::expected::error()` (`!has_value()`) into a trap; the test called it as an argument to an
  ordinary function guarded by `has_value()`, and an ordinary call evaluates every argument. A
  12-line standalone program reproduced it: rc=0 without the flag, SIGILL with it. Sweeping the
  class found exactly two sites, both in one file, with 37 short-circuit sites correctly excluded —
  `&&` does not shield this, `||` and `?:` do.
- A ctest failure that was the harness, not the pin: the TBB sanitizer-reconfigure test globs
  `_deps/*-src` under the parent build directory, and the scratch build had FETCHCONTENT_BASE_DIR
  redirected to share a dependency tree. One symlink, nothing else changed, and it passed.

Neither fix belongs in this repository: the registry fix belongs in sensen, the two-line guard in
SGEE. Both working trees were restored to their pins and nothing was committed here.


## Live site verification, and a gate that accused the engine

Asked to confirm every site was in good working order. The host list was built from
configuration rather than recall -- `frontend/wrangler.toml`, the sibling repositories' deploy
configs, and the hostnames those repositories name -- because a sweep of the sites one happens
to remember passes while a forgotten one is down.

Nine hostnames, four properties. Every browser-facing site answered 200 over HTTP/2 with a
valid certificate, and each was verified FUNCTIONALLY, not just for a status code:

| property | checked | result |
|---|---|---|
| optionsandfuturescalculator.com (+ www) | 56 sitemap routes, 19 referenced assets | all 200 |
| mortgagefvcalculator.com (+ www) | 1247 sitemap routes, 114 referenced assets | all 200 |
| knobugsoft.com | 6 routes, 3 assets, plus the SPA router in a browser | all 200 |
| api.optionsandfuturescalculator.com | a real gRPC-Web `CalculateStrategy` round trip | correct |

The three 404s at `/` were all CORRECT: the API answers 404 to any GET by construction
(`backend/envoy.yaml`), and so do the two Railway service roots. All six health spellings
return 200 "ok", so that class stayed swept.

Numbers were checked against arithmetic derived independently of the engine, never against
its own earlier output:

  - A long 717 call at 10.95 -> break-even 727.95 (strike + premium) and max loss $1,095
    (premium x 100 x 1), both exact.
  - The mortgage default deal -> P&I $3,211 against a closed-form 3210.56; PMI 226.88 -> $227;
    maintenance 5500/12 -> $458; and the six components sum exactly to the $4,716 total.

Two zeros were refused until a control backed them. The route sweep's "0 not 200" means
something on two sites, because a deliberately bogus path returns 404 there -- but knobugsoft
serves index.html for EVERY path (not_found_handling single-page-application), so its 6/6 was
vacuous and only a browser could settle it. In a browser its router resolves deep links and
renders a real 404 view for an unknown path, 547 chars against 4267 for a real page.

The advertising policy was verified live rather than read: ad code appears on
/guides/<strategy> only, and is absent from the calculator root, a /calculator/ page and the
/guides index. A calculator screen is still not publisher content.

Three console errors across the sites were all this box's own DNS blocklist, confirmed by
resolving each host locally (0.0.0.0) and publicly over DoH (real addresses). A developer
machine's null route is not a site defect, and reading it as one is how a healthy site gets
"fixed".

What was actually broken was the gate, not any site: `scripts/probe_live_engine.py` reported
FAILED / OLD ENGINE against a correct, current engine, because a calendar spread is two legs
and the Pro gate now enforces. Full write-up under "Pro tier and quota" in the engineering
notes. Fixed, with all three exit paths exercised and a bogus key as the positive control that
it can still fail.

Open, and NOT acted on because they belong to another property and change DNS or a deploy:
www.knobugsoft.com does not resolve at all while both other properties answer www, and that
site's 404 view is served with HTTP 200 and robots index,follow -- a soft 404 that invites
indexing of every nonexistent URL.
