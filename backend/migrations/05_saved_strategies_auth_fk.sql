-- 05_saved_strategies_auth_fk.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Restores a real foreign key from public.saved_strategies.user_id to
-- auth.users(id), ON DELETE CASCADE.
--
-- THIS CORRECTS MIGRATION 03, WHICH WAS WRONG ABOUT WHY IT COULD NOT DO THIS
--
-- 03's banner states that `auth.users` "lives in SUPABASE's Postgres" while
-- this table lives in "RAILWAY's", and that "a foreign key cannot cross a
-- database". The second half is true and the first half is not. Measured:
--
--   SELECT nspname FROM pg_namespace ...       -> auth, public
--   SELECT to_regclass('auth.users')           -> auth.users
--
-- Auth for this site is SELF-HOSTED GoTrue on Railway
-- (supabase-auth-production-c656.up.railway.app, which the production frontend
-- is built against) and it uses THIS database -- which is also why the role
-- list here contains `supabase_auth_admin` and `authenticator`. There is no
-- database boundary to cross. 03 reasoned from the product's name rather than
-- from the connection string, reached a confident conclusion, and wrote it down
-- as settled.
--
-- The cost 03 accepted -- "deleting a Supabase user no longer cascades here" --
-- was therefore not a necessary cost. This migration removes it.
--
-- WHY THE COLUMN GOES BACK TO UUID
--
-- The subject stored here is a Supabase `sub`, which IS a uuid, and
-- auth.users.id is uuid. Keeping TEXT and casting on every comparison would
-- forfeit the btree index on user_id for the policy's own predicate, on the one
-- column every query in this table filters by.
--
-- GUARDED, because a bare database has no auth schema
--
-- The whole migration is wrapped in a check for auth.users. A developer who
-- creates an empty Postgres and applies 01/03/04/05 has no GoTrue and no
-- auth.users; failing there would make the schema unusable locally for no
-- safety gain, since a database with no auth service has no users to orphan.
--
-- That guard is a place a control could silently go missing, so it does not
-- rely on being remembered: tests/test_strategy_store_pg.cpp section 0 asserts
-- the constraint EXISTS whenever auth.users does, and fails if it does not.

DO $$
BEGIN
    IF to_regclass('auth.users') IS NULL THEN
        RAISE NOTICE
            'auth.users not present -- skipping the FK. This is expected on a bare '
            'local database with no GoTrue, and MUST NOT be the case in production.';
        RETURN;
    END IF;

    -- 1. The policy references user_id, and Postgres refuses to alter the type
    --    of a column a policy depends on. Dropped here and recreated at the end
    --    rather than reordered by hand at apply time.
    DROP POLICY IF EXISTS saved_strategies_own_rows ON public.saved_strategies;

    -- 2. TEXT -> UUID. A no-op on a database where 03 has not been applied.
    IF (SELECT atttypid FROM pg_attribute
         WHERE attrelid = 'public.saved_strategies'::regclass AND attname = 'user_id')
       <> 'uuid'::regtype THEN
        -- A row whose user_id is not a well-formed uuid cannot belong to a real
        -- auth user, and would abort the type change with a cast error.
        DELETE FROM public.saved_strategies
         WHERE user_id !~ '^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$';

        ALTER TABLE public.saved_strategies
            ALTER COLUMN user_id TYPE UUID USING user_id::uuid;
    END IF;

    -- 3. Orphans, cleared unconditionally.
    --
    -- Deliberately OUTSIDE the type-conversion branch above. Rows pointing at a
    -- user auth.users does not have would make ADD CONSTRAINT fail, and those
    -- rows can exist on a database whose column is ALREADY uuid -- for example
    -- one where the constraint was dropped and re-created, which is exactly how
    -- this was found. Scoping the cleanup to the conversion made the migration
    -- succeed the first time and fail every time after, which is the worst
    -- shape for a migration to have.
    DELETE FROM public.saved_strategies s
     WHERE NOT EXISTS (SELECT 1 FROM auth.users u WHERE u.id = s.user_id);

    -- 4. The foreign key, with the cascade that is the whole point.
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                    WHERE conname = 'saved_strategies_user_id_fkey'
                      AND conrelid = 'public.saved_strategies'::regclass) THEN
        ALTER TABLE public.saved_strategies
            ADD CONSTRAINT saved_strategies_user_id_fkey
            FOREIGN KEY (user_id) REFERENCES auth.users(id) ON DELETE CASCADE;
    END IF;

    -- 5. The policy again, now comparing uuid to uuid.
    --
    -- The ::uuid cast RAISES on a malformed setting rather than returning no
    -- rows, so it would turn a bad subject into an error instead of a
    -- fail-closed empty. That is why strategy_store.cpp validates the subject
    -- is a uuid BEFORE any statement runs (see its `is_uuid` guard): the cast
    -- is safe by construction, not by hope. nullif(...,'') still collapses an
    -- unset or empty setting to NULL, and `user_id = NULL` is not true, so
    -- forgetting to set it still yields nothing rather than everything.
    CREATE POLICY saved_strategies_own_rows ON public.saved_strategies
        FOR ALL
        TO ofc_app
        USING (user_id = nullif(current_setting('app.current_user_id', true), '')::uuid)
        WITH CHECK (user_id = nullif(current_setting('app.current_user_id', true), '')::uuid);
END
$$;
