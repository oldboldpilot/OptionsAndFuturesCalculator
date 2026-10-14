# Options & Futures Profit Calculator

@author Olumuyiwa Oluwasanmi

This is the short version: what every session needs on every turn. The full engineering record (each measurement, each
defect's history and the reasoning behind every rule) is `docs/ENGINEERING_NOTES.md`, byte-identical to the former 497 KB
CLAUDE.md. Code comments, migrations and docs that cite "CLAUDE.md" for a section mean that section there, under the same
heading. Find one with grep -n '^## ' docs/ENGINEERING_NOTES.md and read it by line range; never read the file whole.

## Architecture

- Frontend: a static Next.js export served by a Cloudflare Workers static-assets deployment (`frontend/wrangler.toml`) at
  optionsandfuturescalculator.com and www. Not Cloudflare Pages: `wrangler pages deploy` succeeds and changes nothing users see.
- Backend: Railway project fearless-amazement, service options-calculator-backend: the C++23 engine behind Envoy on :8080,
  public at api.optionsandfuturescalculator.com as gRPC-Web and JSON. Native gRPC works only through the TCP proxy
  tokaido.proxy.rlwy.net:34513, because Railway's HTTP edge strips HTTP/2 trailers.
- Railway PostgreSQL holds users, profiles, saved_strategies, state_assumptions and inference_jobs; self-hosted GoTrue runs
  against the same database (so `auth.users` is local).
- One port, three services: calculator.OptionsCalculator (`backend/proto/calculator.proto`), sensen.finance.Finance
  (`backend/proto/finance.proto`) and calculator.assistant.StrategyAssistant (`backend/proto/assistant.proto`). The mortgage
  assistant moved to nest-egg-loan (Railway service mfv-assistant) in 2026-10; nothing here serves it.
- The strategy assistant is a small encoder served in-process by sensen on CPU. With INFERENCE_QUEUE=sgee a busy replica spills
  to the sgee-queue-1/2/3 Raft cluster; PostgreSQL stays the system of record.

## Rules that decide correctness

- Money is a decimal STRING where sensen computes in BigDecimal (38 places) and a double where it computes in double. Never pin
  the digit count of a money string.
- Entitlement gates (`check_strategy_entitlement`, `check_assistant_entitlement`) live in the engine, never the proxy.
  PRO_GATE_MODE is enforce by decision. FINANCE_REQUIRE_KEY is engine-wide: read its section before changing it.
- Saved scenarios: row-level security through the NOLOGIN role ofc_app, entered with SET LOCAL ROLE per transaction; the
  subject comes only from a verified token subject.
- Ads: Google ad code ships only from `frontend/src/app/guides/[strategy]/layout.tsx`, and `frontend/src/config/ad-routes.ts`
  is an allowlist. A calculator screen is not publisher content.
- Measure, don't recall. A moved score is first a hypothesis about the harness: quote the holdout's sha256 and the harness.
- Evaluate a model on sensen, never llama.cpp, and assert exactly one engine holds :50051 (SO_REUSEPORT splits requests).

## Build and test

- Configure with the real compiler path, `-DCMAKE_CXX_COMPILER=/usr/local/bin/clang++` (clang 23, libc++), and keep ccache
  as the launcher; the ccache shim path breaks the std module lookup.
- Build the tests with ninja -C backend/build build_tests, then run ctest --test-dir backend/build. Plain `ninja` does not build the test binaries.
  Run one ctest per build directory and redirect its output to a file.
- Set `CCACHE_DISABLE=1` whenever a module interface changed and the binary will be measured, mutated or deployed: ccache does
  not hash imported module interfaces.
- Never initialise sensen's submodules recursively here; it opens a flag gate and breaks the shared std.pcm.
- After moving the sensen pin, run `scripts/sensen_module_closure.py --check`; its standing false positives are in the notes.
- Load config/.env with `scripts/run_with_env.py`, never by sourcing it (the shell strips the JSON quotes).
- Frontend: `cd frontend && npm run build` (which runs `frontend/scripts/check-export.mjs`) and `npm test`.
- Docs: run `python3 tools/check_docs_claims.py` before committing a changed Markdown file; MISSING must be 0.

## Deploy

- Frontend: `cd frontend && npm run build && npx wrangler deploy`.
- Backend: `scripts/railway_deploy.sh`, which refuses a linked service other than the engine. Confirm the cutover from the
  deployment the script prints: its own log shows one "model is LOADED" line per replica and no ERROR lines, timestamped
  after the upload. A green healthcheck, or the log of the service rather than of the deployment, proves nothing.
- Deploy from a dedicated clean checkout, never from a lane worktree.

## Policy

- Code policy: `config/cpp_details.txt` (C++23 modules, import std, RAII, no C code, sanitizers with positive controls).
- Commits: author and committer Olumuyiwa Oluwasanmi <muyiwamc2@gmail.com>, no trailers of any kind, no AI or model names.
- Never print credentials, values from config/.env, or git remote URLs.

## Index of docs/ENGINEERING_NOTES.md

- System Architecture
- gRPC Surface
- Why the OPTIONS side computes in double and the MORTGAGE side does not
- A double published as a 38-place decimal, and a 30-year loan served as 361 months
- Strategy assistant
- Mortgage assistant
- Security posture: what is enforced, and what is only claimed
- Batch rent-vs-buy, and the ceiling that was never the engine
- Rent vs buy, and the zero that was not a silence
- Closing costs (ComputeClosingCosts)
- Saved scenarios, and the row-level security under them
- State assumptions, and why RLS is the WRONG tool here
- Pro tier and quota
- SGEE queue cluster (sgee-queue-1/2/3)
- The split fleet was tried and REVERTED — the weights are on the engine
- LLQ weight store: serving a model from its compressed image
- Memory, and what the Railway bill is actually buying
- P&L matrix price bounds, and the grid that must NOT be shared
- Option chain cache
- Advertising and the publisher-content policy
- Money is formatted in ONE place, and type="number" cannot group
- Theme tokens and the workspace layout
- Features & Capabilities
- Deployment Details
- import std; and the one std.pcm
- sensen builds without CUDA, and the slim list is hand-maintained
- The toolchain is clang 23, and what moving to it found
- The Railway host has AVX-512; the BASELINE deliberately does not
- Build Commands
- The assistants are doing EXTRACTION while served by an architecture that GENERATES
