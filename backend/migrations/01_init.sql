-- PostgreSQL Database Schema for Options & Futures Calculator on Railway
--
-- AUTHORITATIVE FOR WHAT IS LIVE TODAY. This is the schema actually applied
-- to the Railway Postgres (see CLAUDE.md, "Deployment Details"). It is a
-- historical record of what was run, so it is left structurally unedited
-- here rather than patched to match the design in infra/supabase/ and
-- supabase/migrations/ -- those describe a self-hosted Supabase auth layer
-- (GoTrue + PostgREST + RLS) that has NOT been deployed: the `auth` schema
-- on this database is empty and none of the Supabase roles exist. Until that
-- happens, this file's `public.users` (below) is the only users table there
-- is -- it is NOT `auth.users`, and nothing here has row-level security,
-- because nothing here needs it yet: PostgREST is not exposed to the
-- browser, and the only callers of this database are the C++ engine and the
-- billing Cloudflare Worker (workers/billing/src/index.ts), both connecting
-- with their own credentials, not the public Supabase anon key RLS exists to
-- constrain.
--
-- `profiles.tier` below is vestigial for the same reason it was dropped from
-- the Supabase-auth-era schema in
-- supabase/migrations/20260725025756_init_schema.sql: entitlement is decided
-- from `auth.users.app_metadata.tier`, a JWT claim, not from a database row
-- -- nothing in this codebase reads or writes this column (verified by grep
-- across backend, frontend and workers). It is not dropped here, because
-- dropping it from this file would misrepresent a schema that has already
-- been applied without a corresponding `ALTER TABLE ... DROP COLUMN`
-- actually run against Railway.
--
-- Cutting over to self-hosted Supabase auth means writing and testing a real
-- migration that creates `auth.users` (via infra/supabase/00_bootstrap.sql),
-- moves each `public.users` row into it, and repoints the foreign keys in
-- `public.profiles` and `public.saved_strategies` at `auth.users(id)` to
-- match supabase/migrations/20260725025756_init_schema.sql. That migration
-- does not exist yet, and none of this file, 00_bootstrap.sql, or the
-- supabase/migrations/ files should be treated as already reconciled with
-- each other until it does.

-- 1. Users Table
CREATE TABLE IF NOT EXISTS public.users (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  email TEXT UNIQUE NOT NULL,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW() NOT NULL,
  updated_at TIMESTAMP WITH TIME ZONE DEFAULT NOW() NOT NULL
);

-- 2. Profiles Table
CREATE TABLE IF NOT EXISTS public.profiles (
  id UUID PRIMARY KEY REFERENCES public.users(id) ON DELETE CASCADE,
  tier TEXT DEFAULT 'free' CHECK (tier IN ('free', 'pro')),
  subscription_expires_at TIMESTAMP WITH TIME ZONE,
  stripe_customer_id TEXT,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW() NOT NULL
);

-- 3. Saved Strategies Table
CREATE TABLE IF NOT EXISTS public.saved_strategies (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id UUID NOT NULL REFERENCES public.users(id) ON DELETE CASCADE,
  name TEXT NOT NULL,
  symbol TEXT NOT NULL,
  legs JSONB NOT NULL DEFAULT '[]'::jsonb,
  is_public BOOLEAN DEFAULT false,
  created_at TIMESTAMP WITH TIME ZONE DEFAULT NOW() NOT NULL
);

-- Index for fast user strategy lookups
CREATE INDEX IF NOT EXISTS idx_saved_strategies_user_id ON public.saved_strategies(user_id);
CREATE INDEX IF NOT EXISTS idx_saved_strategies_symbol ON public.saved_strategies(symbol);
