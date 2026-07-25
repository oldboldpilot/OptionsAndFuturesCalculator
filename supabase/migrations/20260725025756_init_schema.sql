-- PostgreSQL Schema for Options & Futures Calculator (Supabase)

-- 1. Create Profiles Table
CREATE TABLE public.profiles (
  id uuid NOT NULL REFERENCES auth.users ON DELETE CASCADE,
  created_at timestamp with time zone DEFAULT timezone('utc'::text, now()) NOT NULL,
  tier text DEFAULT 'free'::text CHECK (tier IN ('free', 'pro')),
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

-- Allow users to update their own profile
CREATE POLICY "Users can update own profile" 
ON public.profiles FOR UPDATE 
USING ( auth.uid() = id );

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

-- Users can update their own strategies
CREATE POLICY "Users can update own strategies"
ON public.saved_strategies FOR UPDATE
USING ( auth.uid() = user_id );

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
