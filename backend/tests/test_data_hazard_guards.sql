-- test_data_hazard_guards.sql -- proves migration 08's guards FIRE.
--
-- @author Olumuyiwa Oluwasanmi
--
-- Run against a database with migrations 01-08 applied, AS A SUPERUSER:
--
--   psql -v ON_ERROR_STOP=0 -f backend/tests/test_data_hazard_guards.sql
--
-- The superuser part is the test, not a convenience. Every other access control
-- in this tree is checked as an unprivileged role BECAUSE a superuser walks
-- past it -- migration 04 exists entirely for that reason. These guards claim
-- the opposite property, and a check run as `ofc_app` would pass whether or not
-- that claim is true.
--
-- ON_ERROR_STOP=0 deliberately: every section here EXPECTS an error, and the
-- run is scored on the tally at the bottom rather than on psql's exit code.
\set ON_ERROR_STOP 0
\timing off
\pset pager off

CREATE TEMP TABLE _r(name text, passed boolean, detail text);
CREATE OR REPLACE FUNCTION pg_temp.expect_refusal(label text, stmt text) RETURNS void
LANGUAGE plpgsql AS $$
BEGIN
    BEGIN
        EXECUTE stmt;
        INSERT INTO _r VALUES (label, false, 'STATEMENT SUCCEEDED -- the guard did not fire');
    EXCEPTION WHEN insufficient_privilege THEN
        INSERT INTO _r VALUES (label, true, SQLERRM);
    WHEN OTHERS THEN
        INSERT INTO _r VALUES (label, false, 'wrong error: ' || SQLSTATE || ' ' || SQLERRM);
    END;
END;
$$;
CREATE OR REPLACE FUNCTION pg_temp.expect_success(label text, stmt text) RETURNS void
LANGUAGE plpgsql AS $$
BEGIN
    BEGIN
        EXECUTE stmt;
        INSERT INTO _r VALUES (label, true, 'permitted, as intended');
    EXCEPTION WHEN OTHERS THEN
        INSERT INTO _r VALUES (label, false, 'REFUSED but should be allowed: ' || SQLERRM);
    END;
END;
$$;

\echo '=== 0. posture: the guards are installed on the relations that claim them ==='
SELECT pg_temp.expect_success('event trigger guard_protected_drop_trg exists',
    $$ SELECT 1/count(*) FROM pg_event_trigger
        WHERE evtname='guard_protected_drop_trg' AND evtenabled<>'D' $$);
SELECT pg_temp.expect_success('saved_strategies carries both DML guards',
    $$ SELECT 1/(count(*)-1) FROM pg_trigger
        WHERE tgrelid='public.saved_strategies'::regclass AND NOT tgisinternal
          AND tgname IN ('guard_truncate_trg','guard_mass_delete_trg') $$);

\echo '=== 1. DROP is refused -- as superuser, which RLS and REVOKE cannot do ==='
SELECT pg_temp.expect_refusal('DROP TABLE saved_strategies',
    'DROP TABLE public.saved_strategies');
SELECT pg_temp.expect_refusal('DROP TABLE state_assumptions',
    'DROP TABLE public.state_assumptions');
SELECT pg_temp.expect_refusal('DROP TABLE protected_relations (the guard list itself)',
    'DROP TABLE public.protected_relations');
SELECT pg_temp.expect_refusal('DROP SCHEMA public CASCADE',
    'DROP SCHEMA public CASCADE');

\echo '=== 2. TRUNCATE is refused ==='
SELECT pg_temp.expect_refusal('TRUNCATE saved_strategies',
    'TRUNCATE TABLE public.saved_strategies');
SELECT pg_temp.expect_refusal('TRUNCATE state_assumptions',
    'TRUNCATE TABLE public.state_assumptions');
-- The asymmetry that keeps the guard alive: inference_jobs is a work queue and
-- its own test suite truncates it. Drop-protected, truncate-free, on purpose.
SELECT pg_temp.expect_success('TRUNCATE inference_jobs is still ALLOWED (work queue)',
    'TRUNCATE TABLE public.inference_jobs');

\echo '=== 3. the mass-delete guard, in BOTH directions ==='
-- Both directions, because a guard that refuses everything passes a
-- refusal-only test exactly as a gate that refuses everyone does.
INSERT INTO public.state_assumptions (slug, name, abbr, median_price, median_rent,
                                       property_tax_rate, data_source, data_year)
SELECT 'probe-' || g, 'probe ' || g, 'X' || g, 300000, 2000, 1.1, 'test', 2024
  FROM generate_series(1, 9) g
ON CONFLICT DO NOTHING;

SELECT pg_temp.expect_success('fixture: the 9 probe rows actually inserted',
    $$ SELECT 1/(count(*)-8) FROM public.state_assumptions WHERE name LIKE 'probe %' $$);
SELECT pg_temp.expect_success('a SMALL delete is permitted (under the 25-row floor)',
    $$ DELETE FROM public.state_assumptions WHERE name LIKE 'probe %' $$);

-- Now past the floor AND past the fraction: 60 rows out of ~110.
INSERT INTO public.state_assumptions (slug, name, abbr, median_price, median_rent,
                                       property_tax_rate, data_source, data_year)
SELECT 'bulk-' || g, 'bulk ' || g, 'Y' || g, 300000, 2000, 1.1, 'test', 2024
  FROM generate_series(1, 60) g
ON CONFLICT DO NOTHING;
SELECT pg_temp.expect_success('fixture: the 60 bulk rows actually inserted',
    $$ SELECT 1/(count(*)-59) FROM public.state_assumptions WHERE name LIKE 'bulk %' $$);
SELECT pg_temp.expect_refusal('deleting 60 of ~110 rows in one statement',
    $$ DELETE FROM public.state_assumptions WHERE name LIKE 'bulk %' $$);
SELECT pg_temp.expect_success('the same 60 rows go in batches of 20',
    $$ DO $x$ BEGIN
         FOR i IN 1..3 LOOP
           DELETE FROM public.state_assumptions
            WHERE ctid IN (SELECT ctid FROM public.state_assumptions
                            WHERE name LIKE 'bulk %' LIMIT 20);
         END LOOP;
       END $x$ $$);

\echo '=== 4. the escape hatch works, and does NOT outlive its transaction ==='
BEGIN;
SET LOCAL app.allow_destructive = 'on';
SELECT pg_temp.expect_success('TRUNCATE permitted with the hatch open',
    'TRUNCATE TABLE public.saved_strategies');
COMMIT;
SELECT pg_temp.expect_refusal('TRUNCATE refused again after COMMIT (SET LOCAL died)',
    'TRUNCATE TABLE public.saved_strategies');

\echo
\echo '=== results ==='
SELECT CASE WHEN passed THEN '  PASS' ELSE '  FAIL' END AS r,
       name, left(detail, 96) AS detail
  FROM _r ORDER BY ctid;
SELECT count(*) FILTER (WHERE passed) AS passed,
       count(*) FILTER (WHERE NOT passed) AS failed,
       count(*) AS checks
  FROM _r;
