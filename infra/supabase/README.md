# Self-hosted Supabase on Railway

Auth and data for Options & Futures Calculator, using the open-source Supabase
components against the **Postgres that already exists** in the Railway project.

```
Cloudflare Worker (UI)  ──►  auth.optionsandfuturescalculator.com   (Kong)
                                    ├── /auth/v1/  ──►  GoTrue
                                    └── /rest/v1/  ──►  PostgREST
                                                          │
Cloudflare Worker (billing) ──────────────────────────────┤
                                                          ▼
                              api.optionsandfuturescalculator.com  (C++ engine)
                                                          │
                                            postgres.railway.internal:5432
```

Three new Railway services. **No new database** — GoTrue and PostgREST both
point at the existing one, which is why the app tables and the auth schema end
up in the same place and a join between `profiles` and `auth.users` is possible
at all.

## 1. Prepare the database

```bash
psql "$DATABASE_URL" \
  -v authenticator_password="$AUTHENTICATOR_PASSWORD" \
  -v auth_admin_password="$AUTH_ADMIN_PASSWORD" \
  -f infra/supabase/00_bootstrap.sql
```

Both passwords are **required** and the script refuses to run without them,
creating nothing. They are not defaulted, because `authenticator` is a LOGIN
role — the account PostgREST connects as — and this Postgres is published on a
public TCP proxy. A placeholder left unchanged would be remote database access
from the internet, and the failure would be silent: everything works, and it
works for everyone.

Generate them with `openssl rand -base64 32` and keep them in `config/.env`.
Re-running the script rotates them rather than failing.

This adds the `auth` schema, the four Supabase roles, `auth.uid()`, and — the
part that matters most — **row-level security on `profiles` and
`saved_strategies`**.

RLS is not optional here. The anon key is public: it ships inside the browser
bundle by design. Without RLS, PostgREST would serve every row of
`saved_strategies` to anyone who read that key out of the page. The key says
which *role* you are; the policy says what that role may see.

Note there is deliberately **no update policy on `profiles`** for
`authenticated`. A user must not be able to write their own `tier`. Only the
billing webhook, holding the service role key (which is `BYPASSRLS`), writes
it.

Use the same two passwords in the connection strings below.

Verified against a throwaway Postgres, not asserted:

```
no password variables          refused, zero roles created
with passwords                 roles created, only LOGIN roles carry one
authenticator can sign in      yes, with the supplied password
user A reads own strategies    own + public only
user A reads B's profile       empty
user upgrades own tier         ERROR: permission denied for table profiles
anon reads strategies          the public one only
re-run                         idempotent, rotates the passwords
```

## 2. Deploy the services

All three read secrets from `config/.env`, which is on the NAS and gitignored.

### `supabase-auth` — GoTrue

Image `supabase/gotrue:v2.151.0`

| Variable | Value |
| --- | --- |
| `GOTRUE_DB_DRIVER` | `postgres` |
| `GOTRUE_DB_DATABASE_URL` | `postgres://supabase_auth_admin:<pw>@postgres.railway.internal:5432/railway` |
| `GOTRUE_JWT_SECRET` | `SUPABASE_JWT_SECRET` |
| `GOTRUE_JWT_EXP` | `3600` |
| `GOTRUE_JWT_DEFAULT_GROUP_NAME` | `authenticated` |
| `GOTRUE_SITE_URL` | `https://optionsandfuturescalculator.com` |
| `GOTRUE_URI_ALLOW_LIST` | `https://optionsandfuturescalculator.com/*` |
| `GOTRUE_DISABLE_SIGNUP` | `false` |
| `GOTRUE_MAILER_AUTOCONFIRM` | `false` |
| `GOTRUE_SMTP_HOST` / `_PORT` / `_USER` / `_PASS` | Resend SMTP |
| `API_EXTERNAL_URL` | `https://auth.optionsandfuturescalculator.com` |
| `PORT` | `9999` |

`GOTRUE_JWT_EXP=3600` is what sets how quickly a cancellation bites: the tier
is a claim inside the access token, so a downgrade takes effect at the next
refresh. An hour is the usual trade between that and refresh traffic.

`GOTRUE_MAILER_AUTOCONFIRM=false` matters — with it on, anyone can sign up as
any address without proving they own it, and the email on a Stripe subscription
is how a payment gets matched to an account.

### `supabase-rest` — PostgREST

Image `postgrest/postgrest:v12.2.0`

| Variable | Value |
| --- | --- |
| `PGRST_DB_URI` | `postgres://authenticator:<pw>@postgres.railway.internal:5432/railway` |
| `PGRST_DB_SCHEMAS` | `public` |
| `PGRST_DB_ANON_ROLE` | `anon` |
| `PGRST_JWT_SECRET` | `SUPABASE_JWT_SECRET` |
| `PGRST_DB_USE_LEGACY_GUCS` | `false` |
| `PGRST_SERVER_PORT` | `3000` |

### `supabase-kong` — gateway

Image `kong:2.8.1`, with `infra/supabase/kong.yml` mounted at
`/home/kong/kong.yml`.

| Variable | Value |
| --- | --- |
| `KONG_DATABASE` | `off` |
| `KONG_DECLARATIVE_CONFIG` | `/home/kong/kong.yml` |
| `KONG_PLUGINS` | `request-transformer,cors,key-auth` |
| `GOTRUE_HOST` | `supabase-auth.railway.internal` |
| `POSTGREST_HOST` | `supabase-rest.railway.internal` |
| `SUPABASE_ANON_KEY` | from `.env` |
| `SUPABASE_SERVICE_ROLE_KEY` | from `.env` |

Attach the custom domain `auth.optionsandfuturescalculator.com` to this service.

## 3. DNS

Same three constraints as `api.` — `CLAUDE.md` records each of them breaking the
endpoint once:

| Record | Target | Proxied |
| --- | --- | --- |
| `auth` CNAME | the **per-domain** endpoint Railway mints for this service | **no** |
| `_railway-verify.auth` TXT | as issued | n/a |

Un-proxied, and pointed at the per-domain endpoint rather than the service
domain. Behind Cloudflare's proxy Railway sees anycast addresses instead of a
CNAME and can neither verify ownership nor complete the ACME challenge.

## 4. Wire the rest

**Engine** (Railway, `options-calculator-backend`):

```
SUPABASE_JWT_SECRET=<same value>
PRO_GATE_MODE=enforce          # only when you are ready to charge
```

The engine verifies access tokens locally with this secret — no call to
Supabase on the request path, so a Supabase outage cannot take pricing down.

**Frontend**:

```
NEXT_PUBLIC_SUPABASE_URL=https://auth.optionsandfuturescalculator.com
NEXT_PUBLIC_SUPABASE_ANON_KEY=<SUPABASE_SELFHOST_ANON_KEY>
```

## One secret, four places

`SUPABASE_JWT_SECRET` is used by GoTrue (signs), PostgREST (verifies), the
engine (verifies) and to generate the anon/service_role keys (which are
themselves JWTs signed with it).

Rotating it invalidates both keys and signs every user out. That is not a
reason never to rotate it — it is a reason to do it deliberately, in one change
that updates all four.

## What this is not doing

- **No Realtime, Storage or Studio.** Nothing in this product subscribes to
  changes, stores files, or needs a DB admin UI in production. They can be
  added later; deploying them now would be three more services to secure and
  patch for no current use.
- **No second Postgres.** The existing one already holds the app tables. A
  separate auth database would put `auth.users` and `public.profiles` in
  different instances, and the foreign key between them is the thing that makes
  a profile row exist for every account.
