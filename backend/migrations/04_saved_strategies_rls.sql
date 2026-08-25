-- 04_saved_strategies_rls.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Row-level security for public.saved_strategies: the database itself refuses
-- to return or accept a row belonging to anyone but the caller in hand.
--
-- WHY THIS IS NOT JUST "ENABLE ROW LEVEL SECURITY"
--
-- The engine connects to Railway Postgres as `postgres`. Measured, not assumed:
--
--   SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = current_user;
--   -> t | t
--
-- Postgres ALWAYS bypasses row security for a superuser or a role carrying
-- BYPASSRLS. So `ENABLE ROW LEVEL SECURITY` on its own -- and `FORCE`, which
-- only removes the TABLE OWNER's exemption, not a superuser's -- would have
-- been inert on every query this engine makes. The policy would exist, `\d`
-- would show it, and it would filter nothing. That is worse than no RLS,
-- because it reads as protection.
--
-- Three ways out were considered:
--
--   (a) point DATABASE_URL at a new least-privilege login role. Real, but it
--       needs a password (a secret this file must not carry), it is a Railway
--       env change, and that URL also serves the inference queue -- so getting
--       the grants wrong takes the assistants down, not just this feature.
--   (b) give the strategy store its own second connection string. Same secret
--       problem, plus a second pool and a second thing to configure.
--   (c) keep one connection and drop privilege PER TRANSACTION with
--       `SET LOCAL ROLE`.
--
-- (c) is taken. A superuser may SET ROLE to any role; inside that transaction
-- `current_user` IS the unprivileged role, so the superuser bypass no longer
-- applies and the policy below genuinely filters. It reverts automatically at
-- COMMIT or ROLLBACK, so a pooled connection is never handed on with reduced
-- privilege or a leaked setting. No password, no new env var, no change to how
-- anything else reaches this database.
--
-- Verified on the live database before this file was written: the same table
-- returns 2 rows as `postgres` and 1 row after `SET LOCAL ROLE`, with the
-- policy and the setting unchanged between the two reads.
--
-- WHAT THIS DOES NOT COVER, stated rather than implied:
--   * inference_jobs is deliberately untouched. It is not per-user data -- it
--     is a shared work queue -- and enabling RLS on it would break both
--     assistants for no confidentiality gain.
--   * public.users / public.profiles are untouched. Both are empty, nothing
--     reads or writes them (CLAUDE.md records profiles.tier as dead), and
--     enabling RLS on a table with no policy denies ALL access -- a change that
--     would sit dormant and then break whatever first tried to use them.
--
-- Every statement is idempotent, matching 01_init.sql's convention.

-- 1. The unprivileged role the engine drops into per transaction.
--
-- NOLOGIN is deliberate and load-bearing: nothing should ever connect AS this
-- role, so it needs no password and cannot be used to reach the database from
-- outside. It is reached only through SET ROLE from an already-authenticated
-- connection.
--
-- NOSUPERUSER/NOBYPASSRLS are the whole point -- either one would put us back
-- to the inert-policy case above. They are restated in the ALTER below so that
-- a role which already exists (from an earlier run, or created by hand) is
-- hardened rather than trusted.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ofc_app') THEN
        CREATE ROLE ofc_app NOLOGIN;
    END IF;
END
$$;

ALTER ROLE ofc_app NOSUPERUSER NOBYPASSRLS NOCREATEDB NOCREATEROLE NOLOGIN;

-- 2. Exactly the privileges the three RPCs need, and nothing else.
--
-- No TRUNCATE, no REFERENCES, no rights on any other table: if a future query
-- in this store reaches for something it was not meant to touch, it fails
-- loudly here rather than succeeding quietly.
GRANT USAGE ON SCHEMA public TO ofc_app;
GRANT SELECT, INSERT, UPDATE, DELETE ON public.saved_strategies TO ofc_app;

-- 3. Turn row security on.
--
-- FORCE does not defend against the superuser (nothing does), and is set anyway
-- so the protection does not silently disappear if this table's owner is ever
-- changed to a non-superuser role.
ALTER TABLE public.saved_strategies ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.saved_strategies FORCE ROW LEVEL SECURITY;

-- 4. The policy.
--
-- `app.current_user_id` is a custom GUC the engine sets per transaction with
-- set_config(..., is_local => true), so it cannot outlive the transaction and
-- cannot leak onto the next request sharing that pooled connection.
--
-- Two details are load-bearing:
--
--   * current_setting(..., missing_ok => true) returns NULL when the engine has
--     not set it. `user_id = NULL` is NULL, which is not true, so the row is
--     invisible and an INSERT is refused. The policy fails CLOSED: forgetting
--     to set the subject yields nothing, never everything.
--   * nullif(..., '') collapses the empty string to NULL too. Without it, a
--     caller who somehow reached here with an empty subject would match rows
--     whose user_id is also empty. The service already refuses an empty
--     subject; this makes the database refuse it independently, which is the
--     entire point of a second layer.
--
-- USING governs what is visible to SELECT/UPDATE/DELETE; WITH CHECK governs
-- what INSERT/UPDATE may write. Both are needed: USING alone would let a caller
-- INSERT a row under someone else's user_id and then be unable to see it.
DROP POLICY IF EXISTS saved_strategies_own_rows ON public.saved_strategies;
CREATE POLICY saved_strategies_own_rows ON public.saved_strategies
    FOR ALL
    TO ofc_app
    USING (user_id = nullif(current_setting('app.current_user_id', true), ''))
    WITH CHECK (user_id = nullif(current_setting('app.current_user_id', true), ''));
