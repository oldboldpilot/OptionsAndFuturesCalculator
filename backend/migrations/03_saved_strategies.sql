-- 03_saved_strategies.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Makes public.saved_strategies actually writable by the engine.
--
-- ############################################################################
-- CORRECTED BY 05_saved_strategies_auth_fk.sql. READ THIS FIRST.
--
-- The banner below argues at length that a foreign key to auth.users is
-- IMPOSSIBLE because auth.users lives in "SUPABASE's Postgres" and this table
-- lives in "RAILWAY's". That is wrong. Auth for this site is self-hosted GoTrue
-- running on Railway against THIS database -- `SELECT to_regclass('auth.users')`
-- returns a table, and the role list here contains supabase_auth_admin. There
-- is no database boundary.
--
-- Migration 05 restores the foreign key with ON DELETE CASCADE. The "known gap"
-- this file records at the bottom -- deleting a user not reaping their rows --
-- is closed, and was never a necessary cost.
--
-- The rest of the banner is left as written rather than edited, because the
-- mistake is instructive: it reasoned from the PRODUCT'S NAME instead of from
-- the connection string, and wrote the conclusion down as settled fact.
-- ############################################################################
--
-- WHY THIS MIGRATION EXISTS AT ALL
--
-- 01_init.sql created the table with
--   user_id UUID NOT NULL REFERENCES public.users(id)
-- and its own banner (lines 31-35) records that public.users/public.profiles/
-- public.saved_strategies "should be repointed at auth.users(id)" by a Supabase
-- migration that "does not exist yet".
--
-- That repoint is NOT POSSIBLE, and the reason is worth stating plainly rather
-- than leaving as a TODO nobody can close: `auth.users` lives in SUPABASE's
-- Postgres. This table lives in RAILWAY's. A foreign key cannot cross a
-- database boundary. So the choice was never "repoint the FK" -- it was between
--
--   (a) mirroring Supabase's whole user directory into public.users so the FK
--       has something to point at, which makes this service a second, always
--       stale copy of an identity system it does not own, or
--   (b) storing the VERIFIED subject as text and letting Supabase stay the sole
--       source of truth for who a user is.
--
-- (b) is taken. `user_id` becomes TEXT and holds the `sub` claim of a
-- signature-verified Supabase access token (see api_key.cpp's
-- verify_supabase_jwt). Nothing else is ever written there: the engine refuses
-- the RPC outright when it cannot resolve a verified subject, so an
-- unauthenticated or API-key-only caller produces no row rather than a row
-- under some shared placeholder id.
--
-- The dropped FK cost one real thing -- ON DELETE CASCADE. Deleting a Supabase
-- user no longer reaps their rows here automatically. That is a known gap, not
-- an oversight; it needs a deletion webhook or a periodic sweep, and neither
-- exists yet.
--
-- Every statement is IF EXISTS / IF NOT EXISTS, matching 01_init.sql's own
-- convention, so re-running this file is a no-op.

-- 1. Drop the cross-database foreign key (see banner).
--
-- The constraint name is Postgres's default for this column, which is what
-- 01_init.sql's inline REFERENCES produced. IF EXISTS covers a database where
-- it was never created.
ALTER TABLE public.saved_strategies
  DROP CONSTRAINT IF EXISTS saved_strategies_user_id_fkey;

-- 2. UUID -> TEXT.
--
-- USING is required because Postgres will not implicitly cast uuid to text in
-- an ALTER COLUMN TYPE. Both live tables are empty at the time of writing, so
-- this rewrites nothing; the cast is here so the statement is also correct on a
-- database that does have rows.
ALTER TABLE public.saved_strategies
  ALTER COLUMN user_id TYPE TEXT USING user_id::text;

-- 3. The saved scenario itself.
--
-- One JSONB column holding a whole calculator.StrategyRequest, written by
-- protobuf's MessageToJsonString and read back by JsonStringToMessage -- so the
-- stored shape IS the wire contract, and a field added to StrategyRequest is
-- carried here with no schema change and no second definition to keep in sync.
--
-- Storing the binary serialization instead would have been smaller and equally
-- lossless, and was rejected: it is unreadable to psql, so the first person
-- debugging a bad saved scenario would have no way to look at one.
ALTER TABLE public.saved_strategies
  ADD COLUMN IF NOT EXISTS payload JSONB NOT NULL DEFAULT '{}'::jsonb;

ALTER TABLE public.saved_strategies
  ADD COLUMN IF NOT EXISTS updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW() NOT NULL;

-- 4. `legs` is superseded by `payload` and is no longer written.
--
-- Kept rather than dropped -- dropping a column is irreversible and this one
-- costs nothing -- but its NOT NULL has to go, or every insert would have to
-- supply a value for a column the engine does not use.
ALTER TABLE public.saved_strategies
  ALTER COLUMN legs DROP NOT NULL;

-- 5. Save-by-name is an UPSERT, and this index is what makes it one.
--
-- Without it, a user pressing Save twice with the same name gets two rows and
-- no way to tell them apart in a list. ON CONFLICT (user_id, name) needs a
-- unique index on exactly that pair to fire.
--
-- Scoped to (user_id, name), not name alone: two different users naming a
-- scenario "Earnings play" is ordinary, and must not collide.
CREATE UNIQUE INDEX IF NOT EXISTS uq_saved_strategies_user_name
  ON public.saved_strategies(user_id, name);
