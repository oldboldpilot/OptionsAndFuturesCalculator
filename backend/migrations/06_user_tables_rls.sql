-- 06_user_tables_rls.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Row-level security for public.users and public.profiles: extends the
-- database-enforced tenancy boundary introduced in migration 04 to the
-- remaining per-user tables.
--
-- WHY THIS IS NOT JUST "ENABLE ROW LEVEL SECURITY"
--
-- The engine connects to Railway Postgres as `postgres`. Measured, not assumed:
--
--   SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = current_user;
--   -> t | t
--
-- Postgres ALWAYS bypasses row security for a superuser or a role carrying
-- BYPASSRLS. `ENABLE ROW LEVEL SECURITY` on its own -- and `FORCE`, which
-- only removes the TABLE OWNER's exemption, not a superuser's -- would have
-- been inert on every query this engine makes directly as `postgres`. The policy
-- would exist, `\d` would show it, and it would filter nothing.
--
-- Migration 04 solved this by creating `ofc_app`, an unprivileged role with
-- NOLOGIN, NOSUPERUSER, and NOBYPASSRLS. The engine drops into `ofc_app` per
-- transaction using `SET LOCAL ROLE`. Inside that transaction `current_user` IS
-- `ofc_app`, the superuser bypass no longer applies, and the policies below
-- genuinely filter. The role reverts automatically at COMMIT or ROLLBACK.
--
-- MEASURED POSTURE BEFORE THIS MIGRATION
--
-- Verified against the live database:
--
--   SELECT c.relname, c.relrowsecurity, c.relforcerowsecurity, count(p.polname) AS policies
--     FROM pg_class c
--     LEFT JOIN pg_policy p ON p.polrelid = c.oid
--    WHERE c.relname IN ('users', 'profiles', 'saved_strategies', 'inference_jobs')
--    GROUP BY c.relname, c.relrowsecurity, c.relforcerowsecurity;
--   -> saved_strategies | t | t | 1
--   -> users            | f | f | 0
--   -> profiles         | f | f | 0
--   -> inference_jobs   | f | f | 0
--
-- Live row counts at the time of writing:
--
--   SELECT count(*) FROM public.users;            -> 0
--   SELECT count(*) FROM public.profiles;         -> 0
--   SELECT count(*) FROM public.saved_strategies; -> 0
--   SELECT count(*) FROM auth.users;              -> 0
--   SELECT count(*) FROM public.inference_jobs;   -> 50
--
-- LATENT EXPOSURE, NOT ACTIVE
--
-- Both `public.users` and `public.profiles` have 0 rows today. Migration 04
-- deliberately left them untouched because enabling RLS on a table with no
-- policy denies all access by default -- a change that would have sat dormant
-- and broken whatever first tried to use them.
--
-- This migration closes a LATENT exposure rather than an active one. It stops
-- being latent at the first signup. By establishing explicit policies and
-- granting `ofc_app` minimum required privileges now, future reads and writes
-- against `public.users` and `public.profiles` are automatically bound to the
-- caller's verified subject under the same mechanism as `saved_strategies`.
--
-- WHAT THIS LEAVES ALONE
--
--   * public.inference_jobs is deliberately untouched. It is not per-user
--     data -- it is a shared work queue (50 rows live) across engine replicas
--     and the two fine-tuned Qwen3-0.6B assistants (strategy and mortgage).
--     Enabling RLS on it would break both assistants for no confidentiality
--     gain.
--
-- BARE DATABASE COMPATIBILITY
--
-- Unlike migration 05 (which conditionally restores a foreign key to
-- `auth.users` only when GoTrue is present), nothing in this migration
-- references `auth.users`. `public.profiles.id` references `public.users(id)`
-- (created in 01_init.sql), and policies evaluate `app.current_user_id`.
-- This migration applies cleanly and identically to a bare local database
-- and production alike.
--
-- Every statement is idempotent, matching 01_init.sql and 04_saved_strategies_rls.sql.

-- 1. Ensure the unprivileged role exists and remains hardened.
--
-- NOLOGIN ensures no outside connection can ever authenticate as ofc_app.
-- NOSUPERUSER and NOBYPASSRLS ensure Postgres cannot bypass row security.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ofc_app') THEN
        CREATE ROLE ofc_app NOLOGIN;
    END IF;
END
$$;

ALTER ROLE ofc_app NOSUPERUSER NOBYPASSRLS NOCREATEDB NOCREATEROLE NOLOGIN;

-- 2. Privileges on public.users and public.profiles for ofc_app.
--
-- Minimum required DML permissions (SELECT, INSERT, UPDATE, DELETE) and schema
-- USAGE. No DDL, no TRUNCATE, no REFERENCES.
GRANT USAGE ON SCHEMA public TO ofc_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON public.users TO ofc_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON public.profiles TO ofc_app;

-- 3. Turn row-level security on and FORCE it.
--
-- FORCE ensures the table owner (if ever changed to a non-superuser role)
-- cannot accidentally bypass row security. Superuser bypass is neutralized
-- by SET LOCAL ROLE ofc_app.
ALTER TABLE public.users ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.users FORCE ROW LEVEL SECURITY;

ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.profiles FORCE ROW LEVEL SECURITY;

-- 4. Policies for public.users and public.profiles.
--
-- The GUC `app.current_user_id` is set per-transaction via `SET LOCAL` or
-- `set_config('app.current_user_id', ..., is_local => true)`.
--
-- Two properties are load-bearing:
--
--   * current_setting('app.current_user_id', true) returns NULL when unset.
--     `nullif(..., '')` collapses empty strings to NULL. Because `id = NULL`
--     evaluates to NULL (falsy), an unauthenticated transaction or unset GUC
--     fails CLOSED: it sees 0 rows and cannot insert or update.
--
--   * ::uuid cast compares uuid to uuid (both public.users.id and
--     public.profiles.id are UUID primary keys). The engine validates that the
--     caller subject is a well-formed UUID before setting the GUC.
--
--   * Both USING and WITH CHECK are specified. USING governs visibility for
--     SELECT, UPDATE, and DELETE. WITH CHECK governs what may be written by
--     INSERT and UPDATE. Without WITH CHECK, a caller could insert a row under
--     someone else's ID and merely be unable to read it back.
DROP POLICY IF EXISTS users_own_rows ON public.users;
CREATE POLICY users_own_rows ON public.users
    FOR ALL
    TO ofc_app
    USING (id = nullif(current_setting('app.current_user_id', true), '')::uuid)
    WITH CHECK (id = nullif(current_setting('app.current_user_id', true), '')::uuid);

DROP POLICY IF EXISTS profiles_own_rows ON public.profiles;
CREATE POLICY profiles_own_rows ON public.profiles
    FOR ALL
    TO ofc_app
    USING (id = nullif(current_setting('app.current_user_id', true), '')::uuid)
    WITH CHECK (id = nullif(current_setting('app.current_user_id', true), '')::uuid);
