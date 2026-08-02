-- PostgreSQL Schema for Options & Futures Calculator (Supabase)
--
-- STATUS: this is the target schema for when self-hosted Supabase auth
-- (infra/supabase/00_bootstrap.sql) actually gets deployed -- it is what that
-- file's RLS policies and `auth.uid()` are written against (profiles.id
-- references auth.users, which only exists once GoTrue is running). It is
-- NOT what is live today: the Railway Postgres currently has no `auth`
-- schema at all, and the tables actually applied there come from
-- backend/migrations/01_init.sql, which rolls its own public.users instead.
-- Cutting over means writing and testing a real migration from
-- public.users-backed rows to auth.users-backed ones; that migration does
-- not exist yet.
--
-- Supersedes supabase/migrations/20260723000000_initial_schema.sql, which
-- defines the same tables incompatibly (both CREATE TABLE public.profiles --
-- applying both in sequence to a fresh database fails outright) and is kept
-- only for history. See that file's header before touching it.

-- 1. Create Profiles Table
--
-- No `tier` column. It was here in earlier drafts, but nothing in this
-- codebase ever read or wrote profiles.tier: entitlement is decided from
-- `auth.users.app_metadata.tier`, a claim on the Supabase-issued JWT that
-- only the service role (the billing webhook, workers/billing/src/index.ts)
-- can write, and that the engine verifies locally
-- (backend/src/modules/api_key.cppm, verify_supabase_jwt). A `tier` column
-- here would be a second, unread copy of the same fact -- and a dead column
-- is exactly the kind of thing a later, less careful migration re-adds an
-- UPDATE policy for. Simpler to not have it than to keep defending it.
CREATE TABLE public.profiles (
  id uuid NOT NULL REFERENCES auth.users ON DELETE CASCADE,
  created_at timestamp with time zone DEFAULT timezone('utc'::text, now()) NOT NULL,
  subscription_expires_at timestamp with time zone,
  stripe_customer_id text,
  PRIMARY KEY (id)
);

-- Turn on Row Level Security
ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;

-- Allow users to read their own profile
CREATE POLICY "Users can view own profile"
ON public.profiles FOR SELECT
USING ( auth.uid() = id );

-- Deliberately NO update policy for `authenticated`.
--
-- A `USING (auth.uid() = id)` policy with no WITH CHECK does not do what it
-- looks like it does: Postgres only reuses the USING expression as the
-- implicit WITH CHECK for the columns USING actually references (here, just
-- `id`), so a caller who owns the row can still rewrite every OTHER column
-- to anything they like -- confirmed empirically against a throwaway
-- Postgres 14 instance while fixing this file (a user could flip their own
-- row's would-be tier/role column with no WITH CHECK in place, and could
-- not with one). Restricting columns via a mixed policy was considered and
-- rejected: there is no column left in this table a user has a legitimate
-- reason to self-serve update (subscription_expires_at and
-- stripe_customer_id are both billing-derived), so the correct number of
-- writable columns for `authenticated` is zero, and RLS's default-deny for
-- any command with no matching policy already gives exactly that with less
-- code than a trigger would. Only the billing webhook, holding the service
-- role key (BYPASSRLS), writes this row after creation.

-- 2. Create Saved Strategies Table
CREATE TABLE public.saved_strategies (
  id uuid DEFAULT gen_random_uuid() PRIMARY KEY,
  user_id uuid NOT NULL REFERENCES auth.users ON DELETE CASCADE,
  created_at timestamp with time zone DEFAULT timezone('utc'::text, now()) NOT NULL,
  name text NOT NULL,
  symbol text NOT NULL,
  legs jsonb NOT NULL DEFAULT '[]'::jsonb,
  is_public boolean DEFAULT false
);

ALTER TABLE public.saved_strategies ENABLE ROW LEVEL SECURITY;

-- Users can view their own strategies OR public strategies
CREATE POLICY "Users can view their own or public strategies"
ON public.saved_strategies FOR SELECT
USING ( auth.uid() = user_id OR is_public = true );

-- Users can insert their own strategies
CREATE POLICY "Users can insert own strategies"
ON public.saved_strategies FOR INSERT
WITH CHECK ( auth.uid() = user_id );

-- Users can update their own strategies. WITH CHECK is explicit here (rather
-- than left to fall back to USING) so the policy matches
-- infra/supabase/00_bootstrap.sql's `strategies_own` policy for the same
-- table, and so a row can never end up re-pointed at a different user_id.
CREATE POLICY "Users can update own strategies"
ON public.saved_strategies FOR UPDATE
USING ( auth.uid() = user_id )
WITH CHECK ( auth.uid() = user_id );

-- Users can delete their own strategies
CREATE POLICY "Users can delete own strategies"
ON public.saved_strategies FOR DELETE
USING ( auth.uid() = user_id );

-- 3. Trigger to automatically create a profile for new users
CREATE FUNCTION public.handle_new_user()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER SET search_path = public
AS $$
BEGIN
  INSERT INTO public.profiles (id)
  VALUES (new.id);
  RETURN new;
END;
$$;

CREATE TRIGGER on_auth_user_created
  AFTER INSERT ON auth.users
  FOR EACH ROW EXECUTE PROCEDURE public.handle_new_user();
