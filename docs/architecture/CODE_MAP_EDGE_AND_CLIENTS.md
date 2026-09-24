# Edge workers, vendored clients and database code map
@author Olumuyiwa Oluwasanmi

> **How to read a citation in this document.** Every `path:line` here was checked
> mechanically: the file exists and the line is inside it. The line is a pointer to
> a NEIGHBOURHOOD, not a guarantee — a sample of 1,167 citations found the named
> symbol within eight lines of the cited line in about four cases in five, and the
> residue is mostly prose adjacency rather than error. Trust the path and the symbol
> name; re-grep the symbol if the line looks wrong. An attempt to auto-correct the
> residue by moving line numbers made it worse and was reverted, which is why this
> note exists instead of a tighter number.

## Scope

This document provides reference documentation for the edge, client, deployment, and database infrastructure outside the core C++ calculation engine and main Next.js frontend in `OptionsAndFuturesCalculator`:

- Edge Workers:
  - `workers/billing/src/index.ts`
  - `workers/billing/src/licence.ts`
  - `workers/billing/wrangler.toml`
  - `workers/billing/README.md`
- Vendored Client Package:
  - `clients/mortgagefv/proto/finance.proto`
  - `clients/mortgagefv/scripts/gen_proto.sh`
  - `clients/mortgagefv/src/grpc/FinanceServiceClientPb.ts`
  - `clients/mortgagefv/src/grpc/finance_pb.d.ts`
  - `clients/mortgagefv/src/grpc/finance_pb.js`
  - `clients/mortgagefv/example/compute-payment.js`
  - `clients/mortgagefv/package.json`
  - `clients/mortgagefv/README.md`
- Supabase Local & Edge Function Configuration:
  - `supabase/config.toml`
  - `supabase/functions/lead-attribution/index.ts`
  - `supabase/migrations/20260723000000_initial_schema.sql`
  - `supabase/migrations/20260725025756_init_schema.sql`
- Supabase Self-Hosted Gateway and Infrastructure:
  - `infra/supabase/00_bootstrap.sql`
  - `infra/supabase/kong.yml`
  - `infra/supabase/README.md`
- Backend PostgreSQL Database Migrations:
  - `backend/migrations/01_init.sql`
  - `backend/migrations/02_inference_queue.sql`
  - `backend/migrations/03_saved_strategies.sql`
  - `backend/migrations/04_saved_strategies_rls.sql`
  - `backend/migrations/05_saved_strategies_auth_fk.sql`
  - `backend/migrations/06_user_tables_rls.sql`
  - `backend/migrations/07_state_assumptions.sql`
- Deployment Automation and Configuration:
  - `deploy/queue-node/deploy.sh`
  - `deploy/queue-node/set-vars.sh`
  - `deploy/queue-node/gen-tls.sh`
  - `deploy/queue-node/railway.json`
  - `deploy/queue-node/README.md`
  - `deploy/assistant-worker/deploy.sh`
  - `deploy/assistant-worker/railway.json`
  - `deploy/assistant-worker/README.md`
  - `deploy/mfv-gateway/Dockerfile`
  - `deploy/mfv-gateway/Caddyfile`
- Ingress Proxy and Container Bootstrap:
  - `backend/envoy.yaml`
  - `backend/start.sh`

---

## Edge Billing Worker (`workers/billing/`)

### Purpose
The billing worker is a Cloudflare Worker handling Stripe Checkout session creation, completed session licence exchanges, Stripe webhook lifecycle processing (licence minting, email delivery via Resend, and GoTrue subscription tier syncing), and liveness health checks. It separates HTTP routing, webhook cryptographic signature checks, and SMTP integrations from the stateless C++ engine (`workers/billing/src/index.ts:1-18`).

### Exported Symbols and Public Routes

| Symbol / Route | Location | Type | Purpose |
| --- | --- | --- | --- |
| `default.fetch` | `workers/billing/src/index.ts:549-575` | Handler | Worker entrypoint routing `OPTIONS`, `POST /checkout`, `GET /licence`, `POST /webhook`, and `GET /health`. |
| `POST /checkout` | `workers/billing/src/index.ts:114-145` | Route | Creates a Stripe Checkout session in `subscription` mode for plan (`monthly` or `annual`) with trial days. |
| `GET /licence` | `workers/billing/src/index.ts:161-191` | Route | Exchanges a completed checkout session ID (`cs_...`) for an immediate signed Pro licence token. |
| `POST /webhook` | `workers/billing/src/index.ts:365-502` | Route | Verifies Stripe signature, filters by product Price ID, updates Supabase tier, and mints/emails licence. |
| `GET /health` | `workers/billing/src/index.ts:565-570` | Route | Returns HTTP 200 `{ ok: true, supabaseConfigured: boolean }` checking presence of Supabase env vars. |
| `OPTIONS` | `workers/billing/src/index.ts:555-556` | Route | Returns HTTP 204 with CORS preflight headers restricted to allowed origins. |
| `mintLicence` | `workers/billing/src/licence.ts:47-78` | Function | Computes unpadded HMAC-SHA512 token truncated to 256 bits over canonical JSON claims payload. |
| `verifyStripeSignature`| `workers/billing/src/licence.ts:91-126`| Function | Validates Stripe `t=<timestamp>,v1=<sig>` signature over raw body within a 300-second window. |
| `Env` | `workers/billing/src/index.ts:21-42` | Interface | Environment configuration binding Stripe keys, signing secrets, Resend key, and Supabase admin credentials. |
| `LicenceClaims` | `workers/billing/src/licence.ts:41-45`| Interface | Structured claims payload containing `customerId`, `tier`, and `periodEndEpoch`. |

### Invariants, Refusals, and Failure Modes
- **CORS Allowlist**: Restricted strictly to `https://optionsandfuturescalculator.com`, `https://www.optionsandfuturescalculator.com`, and `https://optionsandfuturescalculator.pages.dev` (`workers/billing/src/index.ts:64-68`). Unlisted origins default to `https://optionsandfuturescalculator.com` (`workers/billing/src/index.ts:71`).
- **Checkout Pricing Configuration**: Rejects checkout with HTTP 500 `{"error":"price not configured"}` if `PRICE_MONTHLY` or `PRICE_ANNUAL` is missing from environment (`workers/billing/src/index.ts:124`).
- **Trial Period Days**: Defaults to 7 days if `TRIAL_DAYS` is unset (`workers/billing/src/index.ts:126`; `workers/billing/wrangler.toml:12`). Injected as `subscription_data[trial_period_days]` if > 0 (`workers/billing/src/index.ts:139`).
- **Checkout Upstream Failure**: Returns HTTP 502 with Stripe error message if session creation fails (`workers/billing/src/index.ts:142`).
- **Licence Session Verification**:
  - Requires query parameter `session_id` beginning with `cs_`; returns HTTP 400 `{"error":"missing or malformed session_id"}` otherwise (`workers/billing/src/index.ts:162-165`).
  - Fetches session from Stripe expanding `subscription` (`workers/billing/src/index.ts:167-169`). If Stripe reports error, returns HTTP 404 `{"error":"unknown session"}` (`workers/billing/src/index.ts:170`).
  - Re-checks status: Requires `session.status === 'complete'` AND (`session.payment_status === 'paid'` OR `session.payment_status === 'no_payment_required'`); returns HTTP 402 `{"error":"session is not complete"}` if unconfirmed (`workers/billing/src/index.ts:175-177`).
- **Webhook Authentication**: Verifies `Stripe-Signature` header using Web Crypto HMAC-SHA256 over `${timestamp}.${body}` and constant-time string comparison (`workers/billing/src/licence.ts:120-126`). Rejects requests with HTTP 400 `'invalid signature'` on timestamp delta > 300s, missing header, or signature mismatch (`workers/billing/src/licence.ts:111`; `workers/billing/src/index.ts:371-373`).
- **Handled Stripe Events**:
  - `checkout.session.completed` (`workers/billing/src/index.ts:377`)
  - `customer.subscription.created` (`workers/billing/src/index.ts:378`)
  - `customer.subscription.updated` (`workers/billing/src/index.ts:379`)
  - `customer.subscription.deleted` (`workers/billing/src/index.ts:384`)
  - All other event types return HTTP 200 `'ignored'` (`workers/billing/src/index.ts:389`).
- **Shared Stripe Account Entitlement Guard**:
  - `resolveEventPriceIds` inspects line items via `GET /v1/checkout/sessions/${obj.id}/line_items` (for `checkout.session.completed`) or `obj.items.data[].price.id` (for `customer.subscription.*`) (`workers/billing/src/index.ts:323-363`).
  - If `PRICE_MONTHLY` and `PRICE_ANNUAL` are unset in environment, logs configuration error and fails closed returning HTTP 200 `'ok'` (`workers/billing/src/index.ts:405-410`).
  - If event structure cannot be resolved to price IDs, logs security alert and fails closed returning HTTP 200 `'ok'` (`workers/billing/src/index.ts:413-426`).
  - If resolved price IDs do not intersect `[PRICE_MONTHLY, PRICE_ANNUAL]`, logs and skips returning HTTP 200 `'ok'` (`workers/billing/src/index.ts:427-434`).
- **Subscription Status Transitions**:
  - Deletion/Cancellation: If event is `customer.subscription.deleted` or status is `canceled`, `unpaid`, or `incomplete_expired`, invokes `setSupabaseTier(env, email, 'free')` and returns HTTP 200 `'not reissuing'` (`workers/billing/src/index.ts:453-459`).
  - Past Due Grace: If status is `past_due`, returns HTTP 200 `'past_due: grace, not reissuing'` (`workers/billing/src/index.ts:470-472`). The stored tier remains active, but no fresh licence token is minted with an extended period.
  - Active/Trialing: Mints Pro licence token with period end + 3 days (`GRACE_DAYS`) (`workers/billing/src/licence.ts:39, 57`), sets Supabase tier to `'pro'`, and sends email via Resend (`workers/billing/src/index.ts:474-496`).
- **Licence Token Construction**:
  - Algorithm: HMAC-SHA512 via Web Crypto (`workers/billing/src/licence.ts:63-69`).
  - Payload JSON: `{"s":<customerId>,"t":<tier>,"e":<periodEndEpoch + 259200>,"v":1}` (`workers/billing/src/licence.ts:54-59`).
  - Signature: First 32 bytes (256 bits) of the 64-byte HMAC-SHA512 output: `mac.slice(0, 32)` (`workers/billing/src/licence.ts:77`).
  - Encoding: Unpadded base64url via custom `b64url` function stripping all `=` padding (`workers/billing/src/licence.ts:25-29`).
  - Token Structure: `lk_live_${payloadB64}.${b64url(mac.slice(0, 32))}` (`workers/billing/src/licence.ts:77`).
- **Supabase Tier Synchronization**:
  - Direct GoTrue Admin URL: Connects to `${base}/admin/users?filter=${email}` and `${base}/admin/users/${target.id}` without `/auth/v1` prefix because Kong is not deployed (`workers/billing/src/index.ts:233-255, 275`).
  - Exact Email Matching: Filters query results by exact case-insensitive match (`u.email?.toLowerCase() === email.toLowerCase()`) because GoTrue's `filter` query is a substring match (`workers/billing/src/index.ts:266-269`).
  - Tier Property Field: Written to `app_metadata: { tier }` (`workers/billing/src/index.ts:278`), NOT `user_metadata`. `app_metadata` can only be modified with the `service_role` key, whereas `user_metadata` is editable from client browsers (`workers/billing/src/index.ts:198-205`).
  - Fault Tolerance: Network or HTTP errors in Supabase calls return `false` and do not throw, ensuring the webhook returns HTTP 200 to Stripe (`workers/billing/src/index.ts:285-291`).

### Gotchas
1. **Omission of `customer.subscription.deleted`**: Signed licence keys expire automatically by timestamp (`workers/billing/src/licence.ts:57`). However, the Supabase account tier written into `app_metadata.tier` is stored permanently without an expiration date (`workers/billing/README.md:68-72`; `workers/billing/src/index.ts:443-446`). If `customer.subscription.deleted` is not registered in Stripe, cancelled customers retain Pro status in Supabase indefinitely.
2. **GoTrue Admin Path Prefix**: `SUPABASE_URL` points directly at the GoTrue instance (`https://auth.optionsandfuturescalculator.com`). Prepending `/auth/v1/admin/users` results in an HTTP 404, causing user lookups to fail silently while returning HTTP 200 to Stripe (`workers/billing/src/index.ts:233-248`).
3. **`SUPABASE_SERVICE_ROLE_KEY` Matching**: The secret must be the self-hosted key HMAC-signed with `SUPABASE_JWT_SECRET` (`SUPABASE_SELFHOST_SERVICE_ROLE_KEY` in `config/.env`), not an external hosted Supabase project key, or requests fail with HTTP 403 (`workers/billing/wrangler.toml:33-37`; `workers/billing/src/index.ts:36-39`).
4. **Base64url Padding**: The C++ engine's licence parser rejects `=`. Any padding introduced into the base64url encoder renders issued licences unverifiable by the backend (`workers/billing/src/licence.ts:19-24`).

---

## Vendored Financial Client (`clients/mortgagefv/`)

### Purpose
`clients/mortgagefv/` is a self-contained, vendored gRPC-Web TypeScript/JavaScript client package consuming the `sensen.finance.Finance` service hosted on `https://api.optionsandfuturescalculator.com`. It provides protobuf stubs, type declarations, generation automation, and verification tools for external integrators without requiring repository build dependencies (`clients/mortgagefv/README.md:1-19`).

### Exported Symbols and Structure

| File / Symbol | Location | Type | Purpose |
| --- | --- | --- | --- |
| `proto/finance.proto` | `clients/mortgagefv/proto/finance.proto:1-1202` | Schema | Vendored copy of `sensen.finance.Finance` service contract. |
| `scripts/gen_proto.sh` | `clients/mortgagefv/scripts/gen_proto.sh:1-92` | Script | Compiles `proto/finance.proto` into CommonJS and TypeScript gRPC-Web stubs and checks runtime resolution. |
| `FinanceClient` | `clients/mortgagefv/src/grpc/FinanceServiceClientPb.ts:36` | Class | TypeScript gRPC-Web client providing methods for all `sensen.finance.Finance` RPCs. |
| `compute-payment.js` | `clients/mortgagefv/example/compute-payment.js:1-73` | Script | Standalone Node.js verification script executing `ComputePayment` over gRPC-Web with `xhr2`. |
| `package.json` | `clients/mortgagefv/package.json:1-19` | Config | Declares dependencies on `google-protobuf: 3.21.4` and `grpc-web: ^2.0.2`. |

### Invariants, Refusals, and Failure Modes
- **Client Generation Toolchain**:
  - `protoc` version: `libprotoc 3.19.1` via `grpc-tools@1.13.1` (`clients/mortgagefv/scripts/gen_proto.sh:28-36`).
  - Plugin: `protoc-gen-grpc-web` v1.5.0 (`clients/mortgagefv/scripts/gen_proto.sh:42-48`).
  - Command: `protoc --proto_path=proto --js_out=import_style=commonjs,binary:src/grpc --grpc-web_out=import_style=typescript,mode=grpcwebtext:src/grpc proto/finance.proto` (`clients/mortgagefv/scripts/gen_proto.sh:65-70`).
  - Runtime verification: Scans `FinanceServiceClientPb.ts` for `finance_pb.*` symbols and asserts presence in `finance_pb.js`, aborting on mismatch (`clients/mortgagefv/scripts/gen_proto.sh:79-91`).
- **Endpoint and Pathing**:
  - Ingress URL: `https://api.optionsandfuturescalculator.com` (`clients/mortgagefv/README.md:24`).
  - Path structure: `/sensen.finance.Finance/<Method>` handled by Envoy routing (`clients/mortgagefv/README.md:28`).
- **Headers and Authentication**:
  - Framing headers: `content-type: application/grpc-web-text`, `x-grpc-web: 1` (`clients/mortgagefv/README.md:41-43`).
  - API Key: `x-api-key: pk_live_mfv_93a2802945beb32cc7f248e2eaa8a549d33c278480d8b522` (`clients/mortgagefv/README.md:51`). Tier `partner` (2,400 req/min, 1,200,000 compute-units/hr), origin-bound to `https://mortgagefvcalculator.com` and `https://www.mortgagefvcalculator.com` (`clients/mortgagefv/README.md:54-56`).
- **The Numeric Contract**:
  - `string`: Represents exact fixed-point `sensen::BigDecimal` (`Int256 scaled by 1e38, thirty-eight decimal places` since 2026-09-23; `__int128` scaled by 1e18, 18 places before) (`clients/mortgagefv/proto/finance.proto:35-37`; `clients/mortgagefv/README.md:200-205`). Used for all monetary values, principal, amortization rows, and rates on standard calculators (`PaymentRequest`, `DecimalResponse`, `AmortizationRequest`, `HelocRequest`, `RentalRoiRequest`, etc.) (`clients/mortgagefv/README.md:218-228`).
  - `double`: Used only where calculations are natively floating-point (e.g. `ConvertInterestRate`, `ComputeFisherRate`, `ComputeNpv`, `ComputeIrr`, `AmortizationBatchRequest`, `RefinanceResponse.total_savings_over_life`, `HomeFutureValueResponse.future_property_value`, `RentVsBuyResponse`, `HomeNpvResponse`) (`clients/mortgagefv/README.md:229-239`).
  - Malformed decimal inputs: Evaluated strictly; non-numeric values (e.g. `"12x3"`) fail with `INVALID_ARGUMENT` (code 3): `rate is not a decimal number: "12x3"` (`clients/mortgagefv/README.md:275, 427-428`).
  - Required fields: Omission fails with `INVALID_ARGUMENT` (code 3) naming the missing field (e.g. `rate is required and was not supplied`) (`clients/mortgagefv/README.md:295, 356-360`).
- **Sign Convention**:
  - `ComputePayment` returns a NEGATIVE decimal string (e.g. `-1798.651575458257198999` for $300,000, 6% nominal annual, 360 months) representing cash outflow (`clients/mortgagefv/README.md:112-114, 283-290`; `clients/mortgagefv/example/compute-payment.js:69-70`).
- **Contract Drift Verification**:
  - Tested in CI by `backend/tests/test_vendored_proto_drift.cpp:109-123`, asserting byte-identical equivalence below the provenance header against `backend/proto/finance.proto` and verifying presence of `ComputeRentVsBuyBatch`.

### Gotchas
1. **Periodic vs. Annual Rates**: `PaymentRequest.rate` is the rate PER PERIOD (e.g. `0.005` for 6% annual over 12 months), not the annual nominal rate (`clients/mortgagefv/example/compute-payment.js:42-44`; `clients/mortgagefv/README.md:90`).
2. **Float Parsing Precision Loss**: Calling `parseFloat()` or `Number()` on returned decimal strings truncates 18-decimal-place numbers to IEEE-754 float64, corrupting amortization closure (`clients/mortgagefv/README.md:242-264`).
3. **Transport Rate Limit vs. Quota Errors**: Exceeding Envoy's 10 req/s rate limit yields an HTTP 429 response containing header `x-local-rate-limit: true` without a gRPC status code or trailers (`clients/mortgagefv/README.md:300, 339-346`).
4. **Native gRPC Header Stripping**: Connecting to `https://api.optionsandfuturescalculator.com` with native gRPC causes Railway's HTTP edge to terminate HTTP/2 and strip trailers, resulting in `grpc-status: 2` "Missing :te header" or "Stream removed" (`clients/mortgagefv/README.md:414-417`). gRPC-Web must be used.

---

## Supabase Local & Edge Infrastructure (`supabase/`)

### Purpose
Contains local Supabase CLI configurations, edge function implementations for marketing attribution tracking, and migration definitions for public user profiles, strategies, and CRM leads (`supabase/config.toml:1-21`).

### Exported Symbols and Files

| File | Location | Type | Purpose |
| --- | --- | --- | --- |
| `config.toml` | `supabase/config.toml:1-21` | Config | Configures local Supabase CLI ports, PostgreSQL major version 15, and API settings. |
| `supabase/functions/lead-attribution/index.ts`| `supabase/functions/lead-attribution/index.ts:4-50`| Function | Edge function inserting campaign UTM parameters and contact info into `leads` table. |
| `20260723000000_initial_schema.sql`| `supabase/migrations/20260723000000_initial_schema.sql:1-153`| SQL | Superseded schema draft creating profiles, saved strategies, permalinks, and lead events. |
| `20260725025756_init_schema.sql`| `supabase/migrations/20260725025756_init_schema.sql:1-116`| SQL | Target schema for self-hosted GoTrue auth with hardened RLS and no `profiles.tier` column. |

### Invariants, Refusals, and Failure Modes
- **Local CLI Port Configuration**:
  - API Gateway port: 54321 (`supabase/config.toml:4`).
  - PostgreSQL DB port: 54322; Shadow port: 54320 (`supabase/config.toml:10-11`).
  - Studio UI port: 54323 (`supabase/config.toml:15`).
  - DB Major Version: 15 (`supabase/config.toml:12`).
  - Edge Runtime: Enabled (`supabase/config.toml:18`).
  - Analytics: Disabled (`supabase/config.toml:21`).
- **`lead-attribution` HTTP Server**:
  - Method check: Rejects any non-`POST` request with HTTP 405 "Method Not Allowed" (`supabase/functions/lead-attribution/index.ts:5-7`).
  - Environment dependencies: Requires `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY` (`supabase/functions/lead-attribution/index.ts:14-16`).
  - Payload fields: Extracts `email`, `name`, `utm_source`, `utm_medium`, `utm_campaign`, `utm_term`, `utm_content` and inserts into `leads` table (`supabase/functions/lead-attribution/index.ts:10-32`).
  - Errors: Catches exceptions and returns HTTP 400 with `{"error": error.message}` (`supabase/functions/lead-attribution/index.ts:44-48`). Success returns HTTP 200 with `{"success": true, "lead": [...]}` (`supabase/functions/lead-attribution/index.ts:41-43`).
- **Supabase Migration Progression & Status**:
  - `20260723000000_initial_schema.sql` is SUPERSEDED (`supabase/migrations/20260723000000_initial_schema.sql:3`). It defined enum types `subscription_tier` ('free', 'pro') and `subscription_state` (`supabase/migrations/20260723000000_initial_schema.sql:25-26`), created `public.profiles` with column `tier`, `public.saved_strategies`, `public.shared_permalinks`, and `public.broker_lead_events` (`supabase/migrations/20260723000000_initial_schema.sql:28, 69, 103, 127`).
  - `20260725025756_init_schema.sql` is the TARGET schema for self-hosted GoTrue auth (`supabase/migrations/20260725025756_init_schema.sql:3-12`).
  - Profiles `tier` column omitted: `public.profiles` in `20260725025756_init_schema.sql` deliberately omits `tier` (`supabase/migrations/20260725025756_init_schema.sql:20-29`). Entitlement is decided from JWT claim `auth.users.app_metadata.tier` verified locally in C++ (`supabase/migrations/20260725025756_init_schema.sql:22-26`).
  - Update policy denial: Neither migration defines an `UPDATE` policy for `authenticated` on `public.profiles` (`supabase/migrations/20260723000000_initial_schema.sql:43-49`; `supabase/migrations/20260725025756_init_schema.sql:46-62`). A `USING (auth.uid() = id)` policy without an explicit `WITH CHECK` allows callers to modify unreferenced columns. Only the service role key bypasses RLS to update profiles.
  - Trigger: `handle_new_user()` creates an initial profile row referencing `auth.users(id)` upon signup (`supabase/migrations/20260725025756_init_schema.sql:102-116`).

### Gotchas
1. **Executing Both Migrations in Sequence**: Applying `20260723000000` followed by `20260725025756` fails with `relation "profiles" already exists` because both issue `CREATE TABLE public.profiles` (`supabase/migrations/20260723000000_initial_schema.sql:3-7`).
2. **Current Deployment Reality**: Neither Supabase migration is applied to production Railway Postgres; the live database was initialized by `backend/migrations/01_init.sql`, which created `public.users` rather than referencing `auth.users` (`supabase/migrations/20260725025756_init_schema.sql:7-12`).

---

## Supabase Self-Hosted Gateway and Bootstrap (`infra/supabase/`)

### Purpose
Configures self-hosted open-source Supabase services (GoTrue auth, PostgREST data API, Kong API gateway) running directly against the existing Railway PostgreSQL database (`infra/supabase/README.md:1-22`).

### Exported Symbols and Files

| File | Location | Type | Purpose |
| --- | --- | --- | --- |
| `00_bootstrap.sql` | `infra/supabase/00_bootstrap.sql:1-155` | SQL | Database initialization script creating roles, `auth` schema, `auth.uid()`, and baseline RLS policies. |
| `kong.yml` | `infra/supabase/kong.yml:1-79` | Config | Declarative Kong gateway routing `/auth/v1/` to GoTrue and `/rest/v1/` to PostgREST. |
| `README.md` | `infra/supabase/README.md:1-184` | Docs | Operational documentation for environment variables, secrets, and deployment commands. |

### Invariants, Refusals, and Failure Modes
- **Mandatory Password Arguments**:
  - `00_bootstrap.sql` enforces `:authenticator_password` and `:auth_admin_password` via psql variable checks (`infra/supabase/00_bootstrap.sql:46-56`).
  - If either is missing, execution terminates immediately with `FATAL: -v authenticator_password=... is required` or `FATAL: -v auth_admin_password=... is required` and `\quit` (`infra/supabase/00_bootstrap.sql:48, 54`).
- **Postgres Roles and Privileges**:
  - `anon`: `NOLOGIN NOINHERIT` (`infra/supabase/00_bootstrap.sql:69`).
  - `authenticated`: `NOLOGIN NOINHERIT` (`infra/supabase/00_bootstrap.sql:72`).
  - `service_role`: `NOLOGIN NOINHERIT BYPASSRLS` (`infra/supabase/00_bootstrap.sql:77`).
  - `authenticator`: `NOINHERIT LOGIN`, password set from `:authenticator_password` (`infra/supabase/00_bootstrap.sql:84, 95`).
  - `supabase_auth_admin`: `NOINHERIT CREATEROLE LOGIN`, password set from `:auth_admin_password` (`infra/supabase/00_bootstrap.sql:87, 96`).
  - Role inheritance: `GRANT anon, authenticated, service_role TO authenticator` (`infra/supabase/00_bootstrap.sql:98`).
- **Schema and Functions**:
  - Schema: `CREATE SCHEMA IF NOT EXISTS auth AUTHORIZATION supabase_auth_admin` (`infra/supabase/00_bootstrap.sql:103`).
  - `auth.uid()`: Reads setting `request.jwt.claim.sub` populated by PostgREST and casts to UUID (`infra/supabase/00_bootstrap.sql:114-117`).
- **Row-Level Security Policies in `00_bootstrap.sql`**:
  - `profiles_self_read`: `ON public.profiles FOR SELECT USING (id = auth.uid())` (`infra/supabase/00_bootstrap.sql:131-132`). No update policy for `authenticated`.
  - `strategies_own`: `ON public.saved_strategies FOR ALL USING (user_id = auth.uid()) WITH CHECK (user_id = auth.uid())` (`infra/supabase/00_bootstrap.sql:144-145`).
  - `strategies_public_read`: `ON public.saved_strategies FOR SELECT USING (is_public = true)` granted to `anon` (`infra/supabase/00_bootstrap.sql:153-155`).
- **Kong Declarative Gateway Configuration (`kong.yml`)**:
  - `auth-v1`: Upstream `http://${GOTRUE_HOST}:9999/`, path `/auth/v1/`, `strip_path: true` (`infra/supabase/kong.yml:15-21`). CORS allowed origins: `https://optionsandfuturescalculator.com` and `https://www.optionsandfuturescalculator.com` (`infra/supabase/kong.yml:28-31`).
  - `rest-v1`: Upstream `http://${POSTGREST_HOST}:3000/`, path `/rest/v1/`, `strip_path: true` (`infra/supabase/kong.yml:42-48`). Plugin `key-auth` with key name `apikey` (`infra/supabase/kong.yml:67-72`).
  - Consumers: `anon` credentialed with `${SUPABASE_ANON_KEY}`, `service_role` with `${SUPABASE_SERVICE_ROLE_KEY}` (`infra/supabase/kong.yml:73-79`).

### Gotchas
1. **Safety Precondition for Running `00_bootstrap.sql`**: In a database initialized only with `01_init.sql`, `public.profiles.id` and `public.saved_strategies.user_id` originally referenced `public.users(id)` (`infra/supabase/00_bootstrap.sql:12-25`). Running `00_bootstrap.sql` without repointing the foreign keys to `auth.users(id)` activates RLS policies comparing `auth.uid()` against an ID space GoTrue never minted, causing all user queries to return 0 rows (`infra/supabase/00_bootstrap.sql:18-24`).
2. **Password Interpolation in psql**: Passwords are set via `ALTER ROLE` outside the `DO $$` block (`infra/supabase/00_bootstrap.sql:92-96`). psql does not interpolate variables inside dollar-quoted blocks; doing so inside the block writes the literal string `':authenticator_password'` as the role password (`infra/supabase/00_bootstrap.sql:79-82`).
3. **Single Secret Synchronization**: `SUPABASE_JWT_SECRET` must be identical across GoTrue (token signer), PostgREST (token verifier), the C++ engine (local JWT token verifier), and Kong API key generation (`infra/supabase/README.md:165-174`).

---

## Backend Database Migrations (`backend/migrations/`)

### 01_init.sql
- **What it changes**: Creates the initial baseline application schema: table `public.users` (`id UUID PRIMARY KEY DEFAULT gen_random_uuid()`, `email TEXT UNIQUE NOT NULL`, timestamps), table `public.profiles` (`id UUID PRIMARY KEY REFERENCES public.users(id) ON DELETE CASCADE`, `tier TEXT DEFAULT 'free' CHECK (tier IN ('free', 'pro'))`, `subscription_expires_at`, `stripe_customer_id`), and table `public.saved_strategies` (`id UUID PRIMARY KEY DEFAULT gen_random_uuid()`, `user_id UUID REFERENCES public.users(id) ON DELETE CASCADE`, `name`, `symbol`, `legs JSONB DEFAULT '[]'::jsonb`, `is_public BOOLEAN DEFAULT false`) (`backend/migrations/01_init.sql:37-63`). Creates indexes `idx_saved_strategies_user_id` and `idx_saved_strategies_symbol` (`backend/migrations/01_init.sql:66-67`).
- **What it guards on**: `CREATE TABLE IF NOT EXISTS`, `CREATE INDEX IF NOT EXISTS`.
- **Idempotency**: Fully idempotent.

### 02_inference_queue.sql
- **What it changes**: Creates purely additive database substrate for cluster-wide distributed inference task queuing across options-strategy and mortgage assistant workers. Creates sequence `public.inference_fence` (`backend/migrations/02_inference_queue.sql:37`). Creates table `public.inference_jobs` (`id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY`, `surface TEXT CHECK IN ('strategy', 'mortgage')`, `payload JSONB`, `state TEXT DEFAULT 'pending' CHECK IN ('pending', 'leased', 'done', 'failed', 'dead')`, `attempts INT DEFAULT 0`, `max_attempts INT DEFAULT 2`, `fencing_token BIGINT`, `lease_owner TEXT`, `lease_deadline TIMESTAMPTZ`, `submit_deadline TIMESTAMPTZ`, timestamps, `result JSONB`, `error TEXT`) (`backend/migrations/02_inference_queue.sql:47-69`). Creates three partial indexes: `idx_inference_jobs_pending` on `(surface, id) WHERE state = 'pending'`, `idx_inference_jobs_leased` on `(lease_deadline) WHERE state = 'leased'`, and `idx_inference_jobs_finished` on `(finished_at) WHERE state IN ('done', 'failed', 'dead')` (`backend/migrations/02_inference_queue.sql:79-95`).
- **What it guards on**: `CREATE SEQUENCE IF NOT EXISTS`, `CREATE TABLE IF NOT EXISTS`, `CREATE INDEX IF NOT EXISTS`.
- **Idempotency**: Fully idempotent.

### 03_saved_strategies.sql
- **What it changes**: Prepares `public.saved_strategies` for engine persistence. Drops constraint `saved_strategies_user_id_fkey` (`backend/migrations/03_saved_strategies.sql:65-66`). Alters column `user_id` type from UUID to TEXT via `USING user_id::text` (`backend/migrations/03_saved_strategies.sql:74-75`). Adds column `payload JSONB NOT NULL DEFAULT '{}'::jsonb` to hold serialized `calculator.StrategyRequest` messages (`backend/migrations/03_saved_strategies.sql:87-88`). Adds column `updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()` (`backend/migrations/03_saved_strategies.sql:90-91`). Drops NOT NULL constraint on `legs` (`backend/migrations/03_saved_strategies.sql:98-99`). Creates unique index `uq_saved_strategies_user_name` on `(user_id, name)` to support `ON CONFLICT (user_id, name)` upsert queries (`backend/migrations/03_saved_strategies.sql:109-110`).
- **What it guards on**: `DROP CONSTRAINT IF EXISTS`, `ADD COLUMN IF NOT EXISTS`, `ALTER COLUMN legs DROP NOT NULL`, `CREATE UNIQUE INDEX IF NOT EXISTS`.
- **Idempotency**: Fully idempotent.
- **Correction History**: Migration 03's header asserted that a foreign key to `auth.users` was impossible because `auth.users` lived in "SUPABASE's Postgres" while `saved_strategies` lived in "RAILWAY's Postgres" across a cross-database boundary, accepting a "known gap" where deleting a user in auth would not cascade to saved strategies (`backend/migrations/03_saved_strategies.sql:8-24, 34-55`). This conclusion was later identified as an error and corrected by migration 05.

### 04_saved_strategies_rls.sql
- **What it changes**: Implements row-level security on `public.saved_strategies`. Creates unprivileged application role `ofc_app` (`NOLOGIN NOSUPERUSER NOBYPASSRLS NOCREATEDB NOCREATEROLE`) (`backend/migrations/04_saved_strategies_rls.sql:67-74`). Grants `USAGE ON SCHEMA public` and `SELECT, INSERT, UPDATE, DELETE ON public.saved_strategies TO ofc_app` (`backend/migrations/04_saved_strategies_rls.sql:81-82`). Enables and FORCEs RLS on `public.saved_strategies` (`backend/migrations/04_saved_strategies_rls.sql:89-90`). Creates policy `saved_strategies_own_rows` for ALL to `ofc_app` using `user_id = nullif(current_setting('app.current_user_id', true), '')` for both `USING` and `WITH CHECK` (`backend/migrations/04_saved_strategies_rls.sql:113-118`).
- **What it guards on**: `IF NOT EXISTS` on `pg_roles` check (`backend/migrations/04_saved_strategies_rls.sql:68`), `DROP POLICY IF EXISTS` (`backend/migrations/04_saved_strategies_rls.sql:113`).
- **Idempotency**: Fully idempotent.
- **Privilege Separation Mechanism**: The engine connects to Postgres as superuser `postgres` (`rolbypassrls = true`), which would normally bypass RLS completely (`backend/migrations/04_saved_strategies_rls.sql:10-21`). To enforce policies, the engine executes `SET LOCAL ROLE ofc_app` per transaction, dropping superuser status within the transaction scope and causing Postgres to enforce RLS policies (`backend/migrations/04_saved_strategies_rls.sql:30-43`).

### 05_saved_strategies_auth_fk.sql
- **What it changes**: Corrects the erroneous premise of migration 03. Identifies that auth for the site is self-hosted GoTrue running on Railway against this exact same database (`SELECT to_regclass('auth.users')` returns `auth.users`, and role `supabase_auth_admin` exists) (`backend/migrations/05_saved_strategies_auth_fk.sql:10-24`). Drops policy `saved_strategies_own_rows` (`backend/migrations/05_saved_strategies_auth_fk.sql:58`). Converts column `user_id` back from TEXT to UUID, deleting invalid non-UUID strings (`backend/migrations/05_saved_strategies_auth_fk.sql:61-71`). Deletes orphan rows in `saved_strategies` where `user_id` does not exist in `auth.users` (`backend/migrations/05_saved_strategies_auth_fk.sql:82-83`). Restores foreign key constraint `saved_strategies_user_id_fkey` referencing `auth.users(id) ON DELETE CASCADE` (`backend/migrations/05_saved_strategies_auth_fk.sql:86-92`). Re-creates policy `saved_strategies_own_rows` comparing UUIDs: `user_id = nullif(current_setting('app.current_user_id', true), '')::uuid` for both `USING` and `WITH CHECK` (`backend/migrations/05_saved_strategies_auth_fk.sql:103-107`).
- **What it guards on**: Entire migration wrapped in `IF to_regclass('auth.users') IS NULL THEN RAISE NOTICE ... RETURN; END IF;` (`backend/migrations/05_saved_strategies_auth_fk.sql:48-53`). Column conversion checks `pg_attribute.atttypid <> 'uuid'::regtype` (`backend/migrations/05_saved_strategies_auth_fk.sql:61-63`). Foreign key checks `NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'saved_strategies_user_id_fkey')` (`backend/migrations/05_saved_strategies_auth_fk.sql:86-89`).
- **Idempotency**: Fully idempotent.

### 06_user_tables_rls.sql
- **What it changes**: Extends RLS tenancy boundaries to `public.users` and `public.profiles`. Ensures `ofc_app` role exists with `NOLOGIN NOSUPERUSER NOBYPASSRLS` (`backend/migrations/06_user_tables_rls.sql:86-94`). Grants `USAGE ON SCHEMA public` and `SELECT, INSERT, UPDATE, DELETE` on `public.users` and `public.profiles` to `ofc_app` (`backend/migrations/06_user_tables_rls.sql:100-102`). Enables and FORCEs RLS on both tables (`backend/migrations/06_user_tables_rls.sql:109-114`). Creates policies `users_own_rows` and `profiles_own_rows` for ALL to `ofc_app` using `id = nullif(current_setting('app.current_user_id', true), '')::uuid` for both `USING` and `WITH CHECK` (`backend/migrations/06_user_tables_rls.sql:135-147`). Leaves `public.inference_jobs` untouched as a shared work queue (`backend/migrations/06_user_tables_rls.sql:65-70`).
- **What it guards on**: `IF NOT EXISTS` check in `pg_roles` (`backend/migrations/06_user_tables_rls.sql:88`), `DROP POLICY IF EXISTS` (`backend/migrations/06_user_tables_rls.sql:135, 142`).
- **Idempotency**: Fully idempotent.

### 07_state_assumptions.sql
- **What it changes**: Implements schema, plausibility constraints, and role separation for US Census ACS housing reference data. Creates table `public.state_assumptions` (`slug TEXT PRIMARY KEY`, `name TEXT NOT NULL`, `abbr TEXT NOT NULL`, `median_price NUMERIC`, `property_tax_rate NUMERIC`, `insurance_annual NUMERIC`, `state_income_tax NUMERIC`, `median_rent NUMERIC`, `note TEXT`, `data_source TEXT`, `data_year INT`, `refreshed_at TIMESTAMPTZ`) with CHECK constraints: `median_price BETWEEN 50000 AND 3000000`, `median_rent BETWEEN 300 AND 8000`, and `property_tax_rate BETWEEN 0.05 AND 4` (NULL allowed for unpopulated fields) (`backend/migrations/07_state_assumptions.sql:115-134`). Seeds all 50 US states (`backend/migrations/07_state_assumptions.sql:147-198`). Creates telemetry table `public.job_runs` (`job TEXT PRIMARY KEY`, `status TEXT`, `last_run_at TIMESTAMPTZ`, `last_success_at TIMESTAMPTZ`, `last_error TEXT`, `items_processed INT`) (`backend/migrations/07_state_assumptions.sql:204-211`). Creates role `ofc_refresh` (`NOLOGIN NOSUPERUSER NOBYPASSRLS`) (`backend/migrations/07_state_assumptions.sql:265-277`). Grants `ofc_app` SELECT ONLY on `state_assumptions` and `job_runs` (`backend/migrations/07_state_assumptions.sql:290-292`). Grants `ofc_refresh` SELECT on `state_assumptions`, UPDATE ONLY on the 6 refreshed columns (`median_price, property_tax_rate, median_rent, data_source, data_year, refreshed_at`), and `SELECT, INSERT, UPDATE` on `job_runs` (`backend/migrations/07_state_assumptions.sql:280-289`). Enables and FORCEs RLS on both tables (`backend/migrations/07_state_assumptions.sql:298-303`). Creates public read policies `state_assumptions_public_read` and `job_runs_read` `FOR SELECT TO PUBLIC USING (true)` (`backend/migrations/07_state_assumptions.sql:310-328`). Creates refresh write policy `state_assumptions_refresh_write` for UPDATE to `ofc_refresh` duplicating bounds in `WITH CHECK` (`backend/migrations/07_state_assumptions.sql:330-343`), and `job_runs_refresh_write` for ALL to `ofc_refresh` (`backend/migrations/07_state_assumptions.sql:345-350`).
- **What it guards on**: `CREATE TABLE IF NOT EXISTS`, `INSERT ... ON CONFLICT (slug) DO NOTHING`, `IF NOT EXISTS` in `pg_roles`, `DROP POLICY IF EXISTS`.
- **Idempotency**: Fully idempotent.

---

## Deployment Automation (`deploy/`)

### Queue Node Deployment (`deploy/queue-node/`)
- **Purpose**: Automates deployment, variable management, and TLS generation for the three-node SGEE Raft task queue cluster (`sgee-queue-1`, `sgee-queue-2`, `sgee-queue-3`) (`deploy/queue-node/README.md:1-7`).
- **What it uploads**: An isolated staged directory containing `backend/` and `deploy/queue-node/railway.json` at the root (`deploy/queue-node/deploy.sh:26-64`).
- **Staging Mechanism**:
  - `rsync -a --quiet` excluding build artifacts (`build/`, `build-*/`, `*.log`), model files (`models/`), dependencies (`node_modules/`, `.venv/`, `.git`, `__pycache__/`), and unused submodules/tests (`external/SGEE/web-lsp/`, `vscode-extension/`, `BigBrotherAnalytics/`, `sensen/external/CosyVoice/`, `sensen/eval_samples/`, `sensen/demo_qwen3/`, `sensen/benchmarks/`, `sensen/tests/`, `sensen/python/`, `sensen/examples/`, `sensen/docs/`) (`deploy/queue-node/deploy.sh:52-62`).
  - Prunes dangling symlinks from the stage: `find "${dest}" -xtype l -print -delete` (`deploy/queue-node/deploy.sh:73`).
  - Verifies presence of required files (`backend/Dockerfile.queue-node`, `backend/queue-node-entrypoint.sh`, `backend/CMakeLists.txt`, `backend/external/SGEE/...`, `railway.json`) (`deploy/queue-node/deploy.sh:84-103`).
  - Payload size ceiling: Enforces a hard limit of 400 MB (`deploy/queue-node/deploy.sh:108-113`). Typical payload is ~59 MB (`deploy/queue-node/deploy.sh:44-45`).
- **Preconditions**: `require_volume` queries `railway volume list` and asserts each service has a persistent volume mounted at `/data`, aborting if unattached (`deploy/queue-node/deploy.sh:132-156`).
- **Rollout Verification**:
  - `await_healthy` queries `railway deployment list --service "$1" --environment production --json` for the newest deployment ID and its status (`deploy/queue-node/deploy.sh:190-203, 205-215`).
  - Success criteria: Polled every 20 seconds up to a deadline of 2,100 seconds (35 minutes, accommodating source gRPC compilation) until status equals `SUCCESS` (`deploy/queue-node/deploy.sh:207-218`). Never relies on grepping log markers (`deploy/queue-node/deploy.sh:160-178`).
- **Discriminators for Build vs. Runtime Failures**:
  - Evaluates `meta.imageDigest` from `railway deployment list` JSON (`deploy/queue-node/deploy.sh:201`).
  - Discriminator 1 (`imageDigest === "none"`): Build failure; no image was produced, and no container ran. Emits `railway logs --build ${dep_id}` and greps errors (`deploy/queue-node/deploy.sh:236-246`).
  - Discriminator 2 (`imageDigest !== "none"`): Runtime failure; container started and exited/crashed. Emits `railway logs --deployment ${dep_id}` and greps crash dumps (`deploy/queue-node/deploy.sh:247-252`).
- **Rolling Execution**: Rolling deployment across nodes (`deploy.sh all`) replaces nodes sequentially (`sgee-queue-1`, then `sgee-queue-2`, then `sgee-queue-3`). Each node must achieve `SUCCESS` before the next is deployed, preventing loss of Raft quorum (`deploy/queue-node/deploy.sh:350-376`).
- **Variables and TLS Setup**:
  - `deploy/queue-node/set-vars.sh`: Sets `SGEE_NODE_ID`, `SGEE_PEERS` (`1=sgee-queue-1.railway.internal:50052,2=...:50052,3=...:50052`), `SGEE_ELECTION_TIMEOUT_MS=1500`, `SGEE_HEARTBEAT_MS=300`, and `SGEE_QUEUE_TOKEN` (`deploy/queue-node/set-vars.sh:32, 58-59, 105-116`). Passes `--skip-deploys` on all invocations (`deploy/queue-node/set-vars.sh:107`).
  - Staged Base64 mTLS: Encodes `ca.pem`, `node.pem`, and `node.key` as base64 strings into `SGEE_TLS_CA_CERT_B64`, `SGEE_TLS_CERT_B64`, `SGEE_TLS_KEY_B64` (`deploy/queue-node/set-vars.sh:126-137`). Decoded at startup into `/run/sgee-tls` by `backend/queue-node-entrypoint.sh:47-68`.
  - `deploy/queue-node/gen-tls.sh`: Generates private CA (`ca.pem`), node cert (`node.pem`) covering SANs `sgee-queue-1/2/3.railway.internal`, `localhost`, `::1`, `127.0.0.1` (`serverAuth, clientAuth`), and client cert (`client.pem`) for the engine mirror writer (`clientAuth` only) (`deploy/queue-node/gen-tls.sh:43-95`). Refuses to overwrite existing keys (`deploy/queue-node/gen-tls.sh:31-37`).

### Assistant Worker Deployment (`deploy/assistant-worker/`)
- **Purpose**: Deploys the dedicated backend replica holding the fine-tuned Qwen3-0.6B assistant model weights (options strategy and mortgage TVM) that leases jobs from the shared inference queue (`deploy/assistant-worker/README.md:1-11`).
- **What it uploads**: Staged copy of `backend/` and `deploy/assistant-worker/railway.json` (`deploy/assistant-worker/deploy.sh:61-80`).
- **Staging Mechanism**:
  - Mirrors queue-node exclusions, but specifically excludes `models/*.gguf` (`deploy/assistant-worker/deploy.sh:70`).
  - Invariant: `backend/models/` directory must exist in the stage because `backend/Dockerfile` executes `COPY backend/models/ /model-staged/` (`deploy/assistant-worker/deploy.sh:87-93`).
  - Prunes dangling symlinks (`deploy/assistant-worker/deploy.sh:84-85`).
  - Packages staged tree into `.tar.gz` archive (`deploy/assistant-worker/deploy.sh:101-104`).
- **Upload Mechanism**: Uses direct `curl` POST to Railway Backboard API (`https://backboard.railway.com/project/${PROJECT}/environment/${ENVIRONMENT}/up?serviceId=${SERVICE}`) with `Authorization: Bearer ${TOKEN}` read from `~/.railway/config.json` (`user.accessToken`) (`deploy/assistant-worker/deploy.sh:112-124`). This bypasses the Railway CLI's 30-second client timeout (`deploy/assistant-worker/deploy.sh:108-111`).
- **Success Gate and Verification**:
  - Asserts deployment status via `railway deployment list --service assistant-worker` (`deploy/assistant-worker/deploy.sh:127-128`).
  - Checks log output for loaded model weights: `railway logs --service assistant-worker --deployment <id> | grep 'model is LOADED'` (`deploy/assistant-worker/deploy.sh:130-131`).
  - Invariant: `assistant-worker` must have `MODEL_URL` and `MORTGAGE_MODEL_URL` configured and log "model is LOADED"; the main engine (`options-calculator-backend`) must have them unset and log "NOT LOCAL -- submitting to the shared inference queue" (`deploy/assistant-worker/README.md:33-44`).

### MortgageFV Gateway (`deploy/mfv-gateway/`)
- **Purpose**: Minimal Caddy-based API reverse proxy presenting GoTrue and PostgREST behind a single origin matching Supabase client URL expectations (`deploy/mfv-gateway/Caddyfile:1-9`).
- **Container Definition**: Built from `docker.io/library/caddy:2.8-alpine`, copying `Caddyfile` to `/etc/caddy/Caddyfile` (`deploy/mfv-gateway/Dockerfile:3-5`).
- **Routing Rules (`Caddyfile`)**:
  - Server block: Listens on `:{$PORT:8080}` with `admin off` and `auto_https off` (`deploy/mfv-gateway/Caddyfile:15-20`).
  - Auth route: `handle_path /auth/v1/*` strips `/auth/v1` and reverse proxies to `{$AUTH_UPSTREAM}` setting `header_up Host {upstream_hostport}` (`deploy/mfv-gateway/Caddyfile:21-25`).
  - REST route: `handle_path /rest/v1/*` strips `/rest/v1` and reverse proxies to `{$REST_UPSTREAM}` setting `header_up Host {upstream_hostport}` (`deploy/mfv-gateway/Caddyfile:27-31`).
  - Health check: `handle /health` responds with HTTP 200 `"ok"` (`deploy/mfv-gateway/Caddyfile:35-37`).
  - Fallthrough: Catch-all `handle` responds with HTTP 404 `"not a supabase route"` (`deploy/mfv-gateway/Caddyfile:42-44`).

---

## Envoy Proxy and Startup Bootstrap (`backend/envoy.yaml` & `backend/start.sh`)

### Purpose
Configures Envoy as the public ingress proxy fronting the C++ calculation engine on port 50051. Manages gRPC-Web framing, REST/JSON transcoding, local token-bucket rate limiting, CORS headers, and native gRPC listener injection behind Railway's TCP proxy (`backend/envoy.yaml:28-45`; `backend/start.sh:1-23`).

### Configuration Details (`backend/envoy.yaml`)
- **Admin Interface**: Listening on `0.0.0.0:9901` with access log `/tmp/admin_access.log` (`backend/envoy.yaml:1-4`).
- **Clusters**:
  - `backend_grpc_service`: Target `127.0.0.1:50051`, `connect_timeout: 0.25s`, `type: logical_dns`, `lb_policy: round_robin`, `http2_protocol_options: {}` (`backend/envoy.yaml:12-26`). Key declared before `listeners` so `start.sh` can append dynamic listeners to EOF (`backend/envoy.yaml:6-10`).
- **Listeners and Routes**:
  - `listener_0`: Listening on `0.0.0.0:8080` (`backend/envoy.yaml:46-48`).
  - Route `/healthz`: Direct response HTTP 200 `"ok"` (`backend/envoy.yaml:65-68`).
  - Route `/`: Prefix match routing to cluster `backend_grpc_service` (`backend/envoy.yaml:89-91`).
  - Route Timeout: `120s` (`timeout: 120s`) with `grpc_timeout_header_max: 0s` (`backend/envoy.yaml:92-94`), allowing long CPU generation times for assistant inference.
- **CORS Configuration**:
  - `allow_origin_string_match`: Regex `.*` (`backend/envoy.yaml:96-99`).
  - `allow_methods`: `GET, PUT, DELETE, POST, OPTIONS` (`backend/envoy.yaml:100`).
  - `allow_headers`: `keep-alive,user-agent,cache-control,content-type,content-transfer-encoding,custom-header-1,x-accept-content-transfer-encoding,x-accept-response-streaming,x-user-agent,x-grpc-web,grpc-timeout,x-api-key,authorization` (`backend/envoy.yaml:101`).
  - `max_age`: `1728000` (20 days) (`backend/envoy.yaml:102`).
  - `expose_headers`: `custom-header-1,grpc-status,grpc-message` (`backend/envoy.yaml:103`).
- **HTTP Filter Chain (Load-Bearing Order)**:
  1. `envoy.filters.http.local_ratelimit`:
     - Token bucket: `max_tokens: 100`, `tokens_per_fill: 10`, `fill_interval: 1s` (enforces sustained 10 req/s with burst of 100) (`backend/envoy.yaml:109-112`).
     - Enforcement: 100% enabled and enforced (`backend/envoy.yaml:113-122`).
     - Injected header: `x-local-rate-limit: 'true'` with `OVERWRITE_IF_EXISTS_OR_ADD` on rejection (`backend/envoy.yaml:123-127`).
  2. `envoy.filters.http.grpc_web`: Decodes gRPC-Web framing into native gRPC (`backend/envoy.yaml:128-130`).
  3. `envoy.filters.http.grpc_json_transcoder`:
     - Must follow `grpc_web`: Translates HTTP JSON requests to gRPC against descriptor `/etc/envoy/api_descriptor.pb` (`backend/envoy.yaml:137-157`).
     - Registered services: `calculator.OptionsCalculator`, `sensen.finance.Finance`, `calculator.assistant.StrategyAssistant`, `mortgage.assistant.MortgageAssistant` (`backend/envoy.yaml:158-162`).
     - Settings: `auto_mapping: true`, `convert_grpc_status: true`, `print_options: { always_print_primitive_fields: true }` (`backend/envoy.yaml:163-169`).
  4. `envoy.filters.http.cors`: Handles CORS preflight and headers (`backend/envoy.yaml:170-172`).
  5. `envoy.filters.http.router`: Terminal routing filter (`backend/envoy.yaml:173-175`).

### Container Entrypoint Bootstrap (`backend/start.sh`)
- **Config Initialization**: Copies `/etc/envoy/envoy.yaml` to `/tmp/envoy.runtime.yaml` on each boot (`backend/start.sh:39-40`).
- **Native gRPC Listener Injection**:
  - Trigger: Activated when `GRPC_NATIVE_PORT` is set (`backend/start.sh:42`).
  - TLS Configuration: If `GRPC_TLS_CERT` and `GRPC_TLS_KEY` are provided, writes `/etc/envoy/tls/tls.crt` and `/etc/envoy/tls/tls.key` (chmod 600) and configures `envoy.transport_sockets.tls` with ALPN `["h2"]` (`backend/start.sh:45-66`).
  - Plaintext Fallback: If TLS material is absent and `GRPC_ALLOW_PLAINTEXT=1`, logs warning and runs plaintext (`backend/start.sh:68-75`). If `GRPC_ALLOW_PLAINTEXT` is not set, terminates with fatal exit code 1 (`backend/start.sh:76-83`).
  - Listener Appended: Appends `listener_grpc_native` on `:GRPC_NATIVE_PORT` with `codec_type: http2`, route timeout `120s`, cluster `backend_grpc_service`, and no `grpc_web` or transcoder filters (`backend/start.sh:85-129`).
- **Model Path Verification**: Iterates over `MODEL_PATH` and `MORTGAGE_MODEL_PATH`; unsets the variable if the target file does not exist on disk, ensuring the service cleanly reports models unavailable rather than crashing on dangling paths (`backend/start.sh:142-148`).
- **Line Buffering**: Launches `/app/calculator_engine` using `stdbuf -oL -eL` so stdout/stderr are line-buffered across container pipes, avoiding 4 KB block buffering of startup telemetry (`backend/start.sh:165-171`).
- **Process Supervision**: Starts Envoy (`backend/start.sh:174`). Traps SIGTERM and SIGINT (`backend/start.sh:182`). Uses `wait -n "${BACKEND_PID}" "${ENVOY_PID}"` to detect process termination and stops the entire container immediately if either process exits (`backend/start.sh:185-189`).

---

## Test Coverage

| Test Target / File | Location | Component Exercised | Key Assertions / Verifications |
| --- | --- | --- | --- |
| `test_vendored_proto_drift` | `backend/tests/test_vendored_proto_drift.cpp:87-126` | `clients/mortgagefv/proto/finance.proto` | Verifies vendored proto exists and is byte-identical to `backend/proto/finance.proto` below the header; checks presence of `ComputeRentVsBuyBatch`, amortising request fields, and inflation response fields. |
| `compute-payment.js` | `clients/mortgagefv/example/compute-payment.js:1-73` | `clients/mortgagefv/` | Runnable end-to-end gRPC-Web integration test verifying `ComputePayment` against production, asserting negative cash outflow response. |
| `test_strategy_store_pg` | `backend/tests/test_strategy_store_pg.cpp:90-482` | `backend/migrations/03-05` (`saved_strategies`) | Real Postgres integration tests: section 0 asserts RLS configured; section 1 tests insert/upsert; section 3 tests cross-user tenancy isolation; section 5 tests per-user cap; section 6 tests connection pool poisoning; section 7 tests concurrent subjects; section 8 tests `ON DELETE CASCADE` on `auth.users`. |
| `test_inference_queue_pg` | `backend/tests/test_inference_queue_pg.cpp:138-675` | `backend/migrations/02` (`inference_jobs`) | Tests queue lease semantics, 8 concurrent lessors without double-leasing, fencing token rejection of stale worker completions, submit deadline enforcement, background sweeper lease recovery, and LISTEN/NOTIFY wakeup. |
| `test_inference_admission_pg` | `backend/tests/test_inference_admission_pg.cpp:240-575` | `backend/migrations/02` (`inference_jobs`) | Tests cross-instance task handoff, cross-process task execution, queue failover to local execution on database disconnect, and discard of fenced completions. |
| `test_state_assumptions_gate` | `backend/tests/test_state_assumptions_gate.cpp:116-187` | `backend/migrations/07` (`state_assumptions`) | Tests RPC `RefreshStateAssumptions` write gate: anonymous callers refused with `PERMISSION_DENIED`; Pro callers refused; Partner credentials admitted (passing gate to reach unhooked `FAILED_PRECONDITION`); public `GetStateAssumptions` reads allowed anonymously. |
| `test_state_refresh` | `backend/tests/test_state_refresh.cpp:57-189` | `backend/migrations/07` (`state_assumptions`) | Tests Census ACS row parser: slugification (e.g. `New Hampshire` -> `new-hampshire`), derived property tax rate (`taxes / price * 100`), rejection of `-666666666` sentinels, and boundary validation matching database CHECK constraints. |
| `test_api_key_entitlement` | `backend/tests/test_api_key_entitlement.cpp:99-349` | `workers/billing/src/licence.ts` / Engine Gate | Tests entitlement gate outcome discrimination: maps `Identity.outcome` to specific refusal messages (`no-key`, `malformed`, `expired`, `unknown-key`, `origin-not-allowed`); tests `GateMode::Off` vs `GateMode::Enforce`. |
| `test_quota_tier_label` | `backend/tests/test_quota_tier_label.cpp:96-156` | Quota Enforcer & Billing Tiers | Asserts defined tiers (`pro`) receive configured limits (5/min); asserts undefined tiers fall back to anonymous limit (2/min) and are tagged with `(undefined in QUOTA_POLICY; anonymous limits)`. |
| `queue_node_entrypoint_test.sh` | `backend/tests/integration/queue_node_entrypoint_test.sh:1-114` | `backend/queue-node-entrypoint.sh` | Tests container startup script: unset TLS trio runs plaintext; partial combinations (e.g. CA only, CA+CERT) are rejected with exit 1; valid trio decodes PEMs and stages mode 600 key; corrupt base64 is rejected. |
| `entitlement.test.ts` | `frontend/src/store/entitlement.test.ts:90-220` | Frontend Store / Engine Gate | Verifies frontend routes gRPC status `PERMISSION_DENIED` (7) to `gateDenied` / `UpgradePrompt` rather than generic error state, invariant to refusal message wording. |
| `saved-scenarios.test.ts` | `frontend/src/store/saved-scenarios.test.ts:26-336` | Frontend Strategy Store | Verifies frontend discrimination between `UNAUTHENTICATED` (16) and `PERMISSION_DENIED` (7); tests wire round-trip of `SavedStrategy` protobuf messages and reopen fidelity. |

---

## Open Questions

None. All documented behaviors, bounds, invariants, error paths, and configurations have been verified against the codebase.
