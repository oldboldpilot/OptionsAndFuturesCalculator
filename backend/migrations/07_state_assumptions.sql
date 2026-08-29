-- 07_state_assumptions.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Schema, seed data, plausibility constraints, and row-level security for
-- public.state_assumptions and public.job_runs: supports backend-driven US
-- Census ACS housing reference data refresh and job bookkeeping.
--
-- WHY RLS HERE DIFFERS FROM MIGRATIONS 04 AND 06
--
-- Migrations 04 and 06 established tenancy boundaries for per-user tables
-- (saved_strategies, users, profiles) where every row is scoped to a specific
-- caller subject via `app.current_user_id`.
--
-- `public.state_assumptions` is fundamentally different: it is PUBLIC-READ
-- reference data (US Census Bureau ACS 5-year estimates across all 50 states),
-- not per-user tenant data. There is no user subject or tenant ID to scope on.
--
-- The access model and threat boundary are therefore distinct:
--
--   * READ ACCESS: public by nature. User-facing routes (such as rent-vs-buy
--     and NPV state calculators) and client queries must be able to read all
--     50 state assumptions freely.
--   * WRITE ACCESS: strictly backend-internal. The weekly ACS refresh worker
--     runs as a background maintenance process, not an interactive user action.
--     User-facing paths must never be able to insert, modify, or delete reference
--     rows under any circumstance.
--
-- HOW THE PRIVILEGE SEPARATION WORKS
--
-- As measured and recorded in migration 04:
--
--   SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = current_user;
--   -> t | t
--
-- The backend engine connects to Railway Postgres as `postgres` (superuser,
-- rolbypassrls = true). Superuser connections bypass RLS by design in Postgres
-- and carry full DML capabilities -- this is how the background ACS refresh job
-- writes updates to `state_assumptions` and records telemetry in `job_runs`,
-- identical to how inference workers operate on `public.inference_jobs`.
--
-- User-facing execution paths drop into `ofc_app` per transaction using
-- `SET LOCAL ROLE ofc_app`. `ofc_app` is an unprivileged role with NOLOGIN,
-- NOSUPERUSER, and NOBYPASSRLS.
--
-- For `ofc_app`, two defense-in-depth layers are established here:
--
--   1. Object Privileges: `ofc_app` is granted `SELECT` ONLY. No `INSERT`,
--      `UPDATE`, or `DELETE` grants are issued on either table.
--   2. Row-Level Security: Both tables have RLS enabled and forced, with a
--      permissive `FOR SELECT` policy `USING (true)` for `ofc_app`. Because no
--      write policies exist for `ofc_app`, Postgres RLS fails CLOSED on any
--      write attempt.
--
-- Even if a user-facing query or endpoint suffered a regression, logic flaw, or
-- injection bug, it cannot mutate reference data or alter job bookkeeping.
--
-- WHAT THIS DOES NOT PROTECT AGAINST
--
-- Stated plainly: this does NOT constrain superuser connections. `postgres` has
-- BYPASSRLS. `FORCE ROW LEVEL SECURITY` removes only the table owner's
-- exemption for non-superusers; Postgres rules specify that a superuser is
-- unconstrained by RLS under all circumstances. Superuser safety relies on the
-- engine dropping privilege via `SET LOCAL ROLE ofc_app` whenever servicing
-- user-facing requests.
--
-- WHY CHECK CONSTRAINTS ARE IN THE DATABASE AND NOT ONLY IN C++
--
-- Plausibility bounds (e.g. median price between $50,000 and $3,000,000,
-- rent between $300 and $8,000, property tax rate between 0.05% and 4.0%)
-- are checked in the C++ refresh service before write. However, the database
-- is the authoritative storage boundary. The C++ application is merely one
-- caller, and the database schema outlives any individual binary, script, or
-- manual maintenance session.
--
-- Embedding CHECK constraints directly into the table definition guarantees
-- that corrupt upstream figures (such as Census ACS negative sentinels like
-- -666666666 or parse errors) can never be committed to disk, even if application
-- validation is bypassed, regressed, or disabled.
--
-- Initial seed rows have NULL values for refreshed and editorial columns; the
-- CHECK constraints evaluate to TRUE when values are NULL, allowing clean initial
-- seeding while strictly enforcing bounds on all non-NULL writes.
--
-- Every statement is idempotent (CREATE TABLE IF NOT EXISTS, DROP POLICY IF
-- EXISTS, ON CONFLICT DO NOTHING), matching the project migration convention.

-- 1. Ensure the unprivileged role exists and remains hardened.
--
-- NOLOGIN ensures no outside client can authenticate directly as ofc_app.
-- NOSUPERUSER and NOBYPASSRLS ensure Postgres enforces RLS when dropped into
-- this role via SET LOCAL ROLE.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ofc_app') THEN
        CREATE ROLE ofc_app NOLOGIN;
    END IF;
END
$$;

ALTER ROLE ofc_app NOSUPERUSER NOBYPASSRLS NOCREATEDB NOCREATEROLE NOLOGIN;

-- 2. State assumptions reference table.
--
-- Holds ACS 5-year estimate aggregates and editorial assumptions for all 50
-- US states. Keyed by URL-friendly slug (e.g. 'texas', 'new-york').
--
-- Plausibility CHECK constraints protect data integrity at the database layer:
--   * median_price: $50k to $3M
--   * median_rent: $300 to $8,000 / month
--   * property_tax_rate: 0.05% to 4.0%
--
-- CHECK expressions allow NULL so initial seed rows without refresh data are
-- valid, while rejecting invalid non-NULL numeric values on insert or update.
CREATE TABLE IF NOT EXISTS public.state_assumptions (
    slug TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    abbr TEXT NOT NULL,
    median_price NUMERIC,
    property_tax_rate NUMERIC,
    insurance_annual NUMERIC,
    state_income_tax NUMERIC,
    median_rent NUMERIC,
    note TEXT,
    data_source TEXT,
    data_year INT,
    refreshed_at TIMESTAMPTZ,
    CONSTRAINT chk_state_assumptions_median_price
        CHECK (median_price IS NULL OR median_price BETWEEN 50000 AND 3000000),
    CONSTRAINT chk_state_assumptions_median_rent
        CHECK (median_rent IS NULL OR median_rent BETWEEN 300 AND 8000),
    CONSTRAINT chk_state_assumptions_property_tax_rate
        CHECK (property_tax_rate IS NULL OR property_tax_rate BETWEEN 0.05 AND 4)
);

-- 3. Seed all 50 US states.
--
-- Initial seed populates immutable geographic identifiers (slug, name, abbr).
-- Refreshed metrics (median_price, median_rent, property_tax_rate, data_source,
-- data_year, refreshed_at) and editorial metrics (insurance_annual,
-- state_income_tax, note) remain NULL until populated by the refresh job or
-- editorial curation.
--
-- Territories and non-states (DC, Puerto Rico) are deliberately omitted per the
-- 50-state contract. ON CONFLICT (slug) DO NOTHING ensures safe re-execution
-- without overwriting existing data.
INSERT INTO public.state_assumptions (slug, name, abbr) VALUES
    ('alabama', 'Alabama', 'AL'),
    ('alaska', 'Alaska', 'AK'),
    ('arizona', 'Arizona', 'AZ'),
    ('arkansas', 'Arkansas', 'AR'),
    ('california', 'California', 'CA'),
    ('colorado', 'Colorado', 'CO'),
    ('connecticut', 'Connecticut', 'CT'),
    ('delaware', 'Delaware', 'DE'),
    ('florida', 'Florida', 'FL'),
    ('georgia', 'Georgia', 'GA'),
    ('hawaii', 'Hawaii', 'HI'),
    ('idaho', 'Idaho', 'ID'),
    ('illinois', 'Illinois', 'IL'),
    ('indiana', 'Indiana', 'IN'),
    ('iowa', 'Iowa', 'IA'),
    ('kansas', 'Kansas', 'KS'),
    ('kentucky', 'Kentucky', 'KY'),
    ('louisiana', 'Louisiana', 'LA'),
    ('maine', 'Maine', 'ME'),
    ('maryland', 'Maryland', 'MD'),
    ('massachusetts', 'Massachusetts', 'MA'),
    ('michigan', 'Michigan', 'MI'),
    ('minnesota', 'Minnesota', 'MN'),
    ('mississippi', 'Mississippi', 'MS'),
    ('missouri', 'Missouri', 'MO'),
    ('montana', 'Montana', 'MT'),
    ('nebraska', 'Nebraska', 'NE'),
    ('nevada', 'Nevada', 'NV'),
    ('new-hampshire', 'New Hampshire', 'NH'),
    ('new-jersey', 'New Jersey', 'NJ'),
    ('new-mexico', 'New Mexico', 'NM'),
    ('new-york', 'New York', 'NY'),
    ('north-carolina', 'North Carolina', 'NC'),
    ('north-dakota', 'North Dakota', 'ND'),
    ('ohio', 'Ohio', 'OH'),
    ('oklahoma', 'Oklahoma', 'OK'),
    ('oregon', 'Oregon', 'OR'),
    ('pennsylvania', 'Pennsylvania', 'PA'),
    ('rhode-island', 'Rhode Island', 'RI'),
    ('south-carolina', 'South Carolina', 'SC'),
    ('south-dakota', 'South Dakota', 'SD'),
    ('tennessee', 'Tennessee', 'TN'),
    ('texas', 'Texas', 'TX'),
    ('utah', 'Utah', 'UT'),
    ('vermont', 'Vermont', 'VT'),
    ('virginia', 'Virginia', 'VA'),
    ('washington', 'Washington', 'WA'),
    ('west-virginia', 'West Virginia', 'WV'),
    ('wisconsin', 'Wisconsin', 'WI'),
    ('wyoming', 'Wyoming', 'WY')
ON CONFLICT (slug) DO NOTHING;

-- 4. Job runs telemetry and bookkeeping.
--
-- Records execution status, timestamps, error output, and item counts for
-- scheduled background maintenance tasks (e.g. 'state-assumptions-refresh').
CREATE TABLE IF NOT EXISTS public.job_runs (
    job TEXT PRIMARY KEY,
    status TEXT,
    last_run_at TIMESTAMPTZ,
    last_success_at TIMESTAMPTZ,
    last_error TEXT,
    items_processed INT
);

-- 5. Least-privilege permissions for user-facing role ofc_app.
--
-- ofc_app is granted SELECT ONLY. No INSERT, UPDATE, DELETE, TRUNCATE, or
-- REFERENCES privileges are granted.
-- ---------------------------------------------------------------------------
-- WHY RLS IS NOT THE REAL CONTROL ON THIS TABLE, AND WHAT IS
--
-- Every other RLS migration here (04, 06) protects TENANCY: rows belong to a
-- user, and the policy scopes them to a subject. `state_assumptions` has no
-- tenants. Fifty rows of US Census aggregates, public by definition -- there is
-- no confidentiality to protect. Transplanting an "anon reads / service role
-- writes" policy pair here would read as protection and defend nothing.
--
-- What is worth protecting is INTEGRITY: only the refresh job may change the
-- refreshed columns, it may NEVER touch the editorial ones, and out-of-bounds
-- values must never land. Three controls do that, strongest first:
--
--   1. THE CHECK CONSTRAINTS ABOVE. They bind EVERY writer -- including
--      `postgres`, including a hand-typed psql session, including this
--      migration. Strictly stronger than any RLS `WITH CHECK`, which binds
--      only policy-scoped roles. Measured: `UPDATE ... SET median_price = 5`
--      as superuser is refused by
--      `chk_state_assumptions_median_price`.
--
--   2. COLUMN-LEVEL GRANT to a DEDICATED role, below. `ofc_refresh` may UPDATE
--      exactly six columns and nothing else. `slug`, `name`, `abbr`, `note`,
--      `insurance_annual` and `state_income_tax` are unwritable by the job AT
--      THE DATABASE LEVEL -- so a refresh that tried to overwrite hand-authored
--      editorial content is refused by Postgres, not merely by a code path that
--      remembers not to. No INSERT and no DELETE: the contract is
--      update-by-slug over a pre-populated table, and a 51st row or a missing
--      state is a defect, not a mode of operation.
--
--   3. A SEPARATE ROLE FROM `ofc_app`, which is the per-user store principal
--      and gets SELECT here and nothing more. A saved-strategies bug therefore
--      cannot write census columns, and a census bug cannot touch user rows.
--
-- AND THE HONEST LIMIT, which is the opposite of migration 04's:
--
--   In `saved_strategies`, forgetting to set the subject GUC fails CLOSED --
--   the policy sees NULL and returns nothing. HERE, forgetting `SET LOCAL ROLE
--   ofc_refresh` fails OPEN, because the connection is superuser and everything
--   simply works. The fail-closed property cannot come from the database; it
--   must come from the store code refusing to run the UPDATE if the role drop
--   errors. That is why the CHECK constraints carry the bounds -- they are the
--   only layer still standing in that world.
--
--   Nothing here constrains anyone holding DATABASE_URL. That credential is
--   superuser, which is a pre-existing property of the whole system rather
--   than anything this table introduces.
-- ---------------------------------------------------------------------------

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'ofc_refresh') THEN
        CREATE ROLE ofc_refresh;
    END IF;
END
$$;

-- NOLOGIN is load-bearing: nothing ever connects AS this role, so it needs no
-- password, which is what lets the whole design work with no new secret and no
-- Railway env change. It is reached only via `SET LOCAL ROLE` from the
-- already-authenticated engine connection, exactly as `ofc_app` is.
ALTER ROLE ofc_refresh NOSUPERUSER NOBYPASSRLS NOCREATEDB NOCREATEROLE NOLOGIN;

GRANT USAGE ON SCHEMA public TO ofc_refresh;
GRANT SELECT ON public.state_assumptions TO ofc_refresh;

-- THE SIX REFRESHED COLUMNS, AND ONLY THOSE.
GRANT UPDATE (median_price, property_tax_rate, median_rent,
              data_source, data_year, refreshed_at)
    ON public.state_assumptions TO ofc_refresh;

-- Bookkeeping: the job records its own runs.
GRANT SELECT, INSERT, UPDATE ON public.job_runs TO ofc_refresh;

GRANT USAGE ON SCHEMA public TO ofc_app;
GRANT SELECT ON public.state_assumptions TO ofc_app;
GRANT SELECT ON public.job_runs TO ofc_app;

-- 6. Turn row-level security on and FORCE it.
--
-- FORCE ensures table owners (if changed to a non-superuser role in the future)
-- are subjected to RLS policies.
ALTER TABLE public.state_assumptions ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.state_assumptions FORCE ROW LEVEL SECURITY;

ALTER TABLE public.job_runs ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.job_runs FORCE ROW LEVEL SECURITY;

-- 7. RLS policies for ofc_app.
--
-- Public reference data (state_assumptions) and job status (job_runs) are
-- readable by ofc_app. With no INSERT, UPDATE, or DELETE policies defined for
-- ofc_app, all write operations under this role are rejected by RLS in addition
-- to being refused by Postgres table-level grant checks.
DROP POLICY IF EXISTS state_assumptions_public_read ON public.state_assumptions;
-- TO PUBLIC, not TO ofc_app.
--
-- Two reasons, and the second was a live bug. First, this data IS public --
-- US Census aggregates -- so scoping the read to one application role states
-- something untrue about it. Second, and measured: an UPDATE under RLS must
-- first SEE the row, so a policy naming only `ofc_app` left `ofc_refresh`
-- unable to see anything and every one of the fifty UPDATEs matched zero rows.
-- The run committed, reported ok, and wrote nothing.
CREATE POLICY state_assumptions_public_read ON public.state_assumptions
    FOR SELECT
    TO PUBLIC
    USING (true);

DROP POLICY IF EXISTS job_runs_read ON public.job_runs;
CREATE POLICY job_runs_read ON public.job_runs
    FOR SELECT
    TO PUBLIC
    USING (true);

DROP POLICY IF EXISTS state_assumptions_refresh_write ON public.state_assumptions;
CREATE POLICY state_assumptions_refresh_write ON public.state_assumptions
    FOR UPDATE
    TO ofc_refresh
    USING (true)
    -- Restated here as defence in depth. The CHECK constraints already bind
    -- every writer; this makes the same bounds visible at the policy layer, so
    -- a reader of `\d+ state_assumptions` sees the contract without having to
    -- go and read the column definitions.
    WITH CHECK (
        (median_price IS NULL OR median_price BETWEEN 50000 AND 3000000)
        AND (median_rent IS NULL OR median_rent BETWEEN 300 AND 8000)
        AND (property_tax_rate IS NULL OR property_tax_rate BETWEEN 0.05 AND 4)
    );

DROP POLICY IF EXISTS job_runs_refresh_write ON public.job_runs;
CREATE POLICY job_runs_refresh_write ON public.job_runs
    FOR ALL
    TO ofc_refresh
    USING (true)
    WITH CHECK (true);
