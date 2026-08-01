-- Bootstrap the existing Railway Postgres for self-hosted Supabase.
--
-- Run ONCE, against the database that already holds public.users,
-- public.profiles and public.saved_strategies. It adds only what GoTrue and
-- PostgREST need; it does not touch the application tables.
--
--   psql "$DATABASE_URL" -f infra/supabase/00_bootstrap.sql
--
-- Supabase's own installer assumes an empty database and creates one. This
-- database is not empty, and the app schema in it is the thing we are keeping,
-- so the roles and schemas are declared here explicitly rather than by running
-- an installer that would expect to own the whole instance.

-- ---------------------------------------------------------------------------
-- Roles
--
-- PostgREST switches into `anon` or `authenticated` per request based on the
-- `role` claim in the JWT, so these are the identities RLS policies are written
-- against. NOLOGIN is deliberate: they are assumed via SET ROLE, never
-- connected to directly.
-- ---------------------------------------------------------------------------
DO $$
BEGIN
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'anon') THEN
    CREATE ROLE anon NOLOGIN NOINHERIT;
  END IF;
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'authenticated') THEN
    CREATE ROLE authenticated NOLOGIN NOINHERIT;
  END IF;
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'service_role') THEN
    -- BYPASSRLS is what makes this role able to write a subscription tier the
    -- user themselves must not be able to write.
    CREATE ROLE service_role NOLOGIN NOINHERIT BYPASSRLS;
  END IF;
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'authenticator') THEN
    CREATE ROLE authenticator NOINHERIT LOGIN PASSWORD 'CHANGE_ME_AUTHENTICATOR';
  END IF;
  IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname = 'supabase_auth_admin') THEN
    CREATE ROLE supabase_auth_admin NOINHERIT CREATEROLE LOGIN PASSWORD 'CHANGE_ME_AUTH_ADMIN';
  END IF;
END
$$;

GRANT anon, authenticated, service_role TO authenticator;

-- ---------------------------------------------------------------------------
-- Schemas
-- ---------------------------------------------------------------------------
CREATE SCHEMA IF NOT EXISTS auth AUTHORIZATION supabase_auth_admin;
GRANT USAGE ON SCHEMA public TO anon, authenticated, service_role;

-- ---------------------------------------------------------------------------
-- auth.uid()
--
-- The function every RLS policy is written in terms of. It reads the `sub`
-- claim out of the JWT that PostgREST put into the request-local settings, so a
-- policy can say "this row belongs to the caller" without the caller being able
-- to assert who they are.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION auth.uid() RETURNS uuid
LANGUAGE sql STABLE AS $$
  SELECT NULLIF(current_setting('request.jwt.claim.sub', true), '')::uuid;
$$;

-- ---------------------------------------------------------------------------
-- Row-level security on the application tables.
--
-- Without this, PostgREST would happily serve every row in saved_strategies to
-- anyone holding the anon key -- which is public, because it ships in the
-- browser. RLS is what makes a public key safe to publish: the key says which
-- ROLE you are, and the policy says what that role may see.
-- ---------------------------------------------------------------------------
ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.saved_strategies ENABLE ROW LEVEL SECURITY;

DROP POLICY IF EXISTS profiles_self_read ON public.profiles;
CREATE POLICY profiles_self_read ON public.profiles
  FOR SELECT USING (id = auth.uid());

-- Deliberately NO update policy for `authenticated`. A user must not be able to
-- set their own tier; only the billing webhook, holding the service role key,
-- writes that column.
DROP POLICY IF EXISTS strategies_own ON public.saved_strategies;
CREATE POLICY strategies_own ON public.saved_strategies
  FOR ALL USING (user_id = auth.uid()) WITH CHECK (user_id = auth.uid());

GRANT SELECT ON public.profiles TO authenticated;
GRANT SELECT, INSERT, UPDATE, DELETE ON public.saved_strategies TO authenticated;

-- Public strategies stay readable without an account, which is what makes a
-- shared permalink work for someone who has not signed up.
DROP POLICY IF EXISTS strategies_public_read ON public.saved_strategies;
CREATE POLICY strategies_public_read ON public.saved_strategies
  FOR SELECT USING (is_public = true);
GRANT SELECT ON public.saved_strategies TO anon;
