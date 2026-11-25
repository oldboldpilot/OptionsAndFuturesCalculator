-- 0002_handle_new_user_function
--
-- @author Olumuyiwa Oluwasanmi
--
-- WHY: public.handle_new_user() exists in production and no migration records it. The 0001
-- baseline was replayed from nine legacy files, and this function was never in them. It is
-- this application's own object (it lives in public and INSERTs into public.profiles), and it
-- is SECURITY DEFINER, so it is exactly the kind of fact the drift gate must watch.
--
-- Captured from production 2026-11-25 with pg_get_functiondef: owner postgres, SECURITY
-- DEFINER, search_path pinned to 'public', default EXECUTE (proacl null). Nothing in the
-- database calls it: no trigger references it (measured), so it is currently inert. This
-- migration records the function and deliberately does NOT create a trigger: a trigger on
-- auth.users belongs to GoTrue's schema and would be a behaviour change, not a record.
--
-- Idempotent (CREATE OR REPLACE). Production already holds it, so it is ADOPTED there, not
-- applied. The text below must stay byte-equal to the live definition or the gate goes red.
--
-- Runs in ONE transaction together with its ledger row. Add
-- "-- migrate:no-transaction" as the FIRST line for CREATE INDEX CONCURRENTLY.
-- Roles, grants and RLS policies are schema: put them here, or the drift gate
-- (scripts/check_schema_drift.sh) will say the database moved without a record.
-- Never edit this file once it is sealed or applied; add a newer migration.

CREATE OR REPLACE FUNCTION public.handle_new_user()
 RETURNS trigger
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO 'public'
AS $function$
BEGIN
  INSERT INTO public.profiles (id)
  VALUES (new.id);
  RETURN new;
END;
$function$;
