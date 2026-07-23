-- Initial Schema for Options & Futures Calculator

-- Enable UUID generation
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-------------------------------------------------------------------------------
-- 1. PROFILES TABLE
-------------------------------------------------------------------------------
CREATE TYPE subscription_tier AS ENUM ('free', 'pro');
CREATE TYPE subscription_state AS ENUM ('active', 'past_due', 'canceled', 'incomplete', 'trialing');

CREATE TABLE public.profiles (
    id UUID PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    tier subscription_tier NOT NULL DEFAULT 'free',
    stripe_customer_id TEXT UNIQUE,
    subscription_status subscription_state,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Users can view own profile" 
    ON public.profiles FOR SELECT 
    USING (auth.uid() = id);

CREATE POLICY "Users can update own profile" 
    ON public.profiles FOR UPDATE 
    USING (auth.uid() = id);

-- Function to automatically create a profile when a new user signs up
CREATE OR REPLACE FUNCTION public.handle_new_user()
RETURNS TRIGGER AS $$
BEGIN
  INSERT INTO public.profiles (id, tier)
  VALUES (NEW.id, 'free');
  RETURN NEW;
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

CREATE TRIGGER on_auth_user_created
  AFTER INSERT ON auth.users
  FOR EACH ROW EXECUTE PROCEDURE public.handle_new_user();


-------------------------------------------------------------------------------
-- 2. SAVED STRATEGIES TABLE
-------------------------------------------------------------------------------
CREATE TABLE public.saved_strategies (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    symbol TEXT NOT NULL,
    strategy_type TEXT NOT NULL, -- e.g., 'iron_condor', 'straddle'
    legs_jsonb JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.saved_strategies ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Users can view own strategies" 
    ON public.saved_strategies FOR SELECT 
    USING (auth.uid() = user_id);

CREATE POLICY "Users can insert own strategies" 
    ON public.saved_strategies FOR INSERT 
    WITH CHECK (auth.uid() = user_id);

CREATE POLICY "Users can update own strategies" 
    ON public.saved_strategies FOR UPDATE 
    USING (auth.uid() = user_id);

CREATE POLICY "Users can delete own strategies" 
    ON public.saved_strategies FOR DELETE 
    USING (auth.uid() = user_id);


-------------------------------------------------------------------------------
-- 3. SHARED PERMALINKS TABLE
-------------------------------------------------------------------------------
CREATE TABLE public.shared_permalinks (
    hash_id TEXT PRIMARY KEY, -- e.g., Base62 short hash
    user_id UUID REFERENCES auth.users(id) ON DELETE SET NULL, -- Optional: link to creator
    strategy_payload_jsonb JSONB NOT NULL,
    views INT NOT NULL DEFAULT 0,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

ALTER TABLE public.shared_permalinks ENABLE ROW LEVEL SECURITY;

-- Permalinks are readable by ANYONE (public)
CREATE POLICY "Permalinks are publically readable" 
    ON public.shared_permalinks FOR SELECT 
    USING (true);

-- Only authenticated users can create a permalink (or we can allow anon if we want)
CREATE POLICY "Users can create permalinks" 
    ON public.shared_permalinks FOR INSERT 
    WITH CHECK (auth.role() = 'authenticated');


-------------------------------------------------------------------------------
-- 4. BROKER LEAD EVENTS TABLE (CPA Tracking)
-------------------------------------------------------------------------------
CREATE TABLE public.broker_lead_events (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    user_id UUID REFERENCES auth.users(id) ON DELETE SET NULL,
    broker_name TEXT NOT NULL, -- e.g., 'tastytrade', 'schwab'
    strategy_type TEXT,
    utm_source TEXT,
    utm_medium TEXT,
    utm_campaign TEXT,
    conversion_status TEXT DEFAULT 'click', -- 'click', 'account_opened', 'funded'
    clicked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    converted_at TIMESTAMPTZ
);

ALTER TABLE public.broker_lead_events ENABLE ROW LEVEL SECURITY;

-- Users can view their own outbound clicks (optional feature)
CREATE POLICY "Users can view own lead events" 
    ON public.broker_lead_events FOR SELECT 
    USING (auth.uid() = user_id);

-- Only the backend edge functions (service role) can update conversion status
-- So no UPDATE policy is provided for regular users.

-- Inserts are allowed via authenticated users or anon (if we want to track logged-out leads)
CREATE POLICY "Anyone can insert a lead event" 
    ON public.broker_lead_events FOR INSERT 
    WITH CHECK (true);
