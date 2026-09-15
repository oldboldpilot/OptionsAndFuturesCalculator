-- Security posture of the live database, asserted rather than assumed.
--
-- @author Olumuyiwa Oluwasanmi
--
-- WHY THIS EXISTS, AND WHY IT IS SEPARATE FROM test_data_hazard_guards.sql.
-- That file proves the guards REFUSE things: DROP, TRUNCATE, an unbounded
-- DELETE. It exercises behaviour, and behaviour is exactly what cannot see the
-- posture underneath it. Dropping an RLS policy changes no answer this
-- application returns, because the application's own WHERE clause produces the
-- identical rows either way -- the same reason section 0 of
-- test_strategy_store_pg asserts relrowsecurity directly. A posture that only
-- behaviour checks is a posture nobody is checking.
--
-- It was written after a migration reached production with two defects that a
-- green test suite could not see:
--
--   1. `guard_mass_delete()` was SECURITY INVOKER, so it read its own guard list
--      as the CALLING role and turned every application DELETE into
--      `permission denied for table protected_relations`.
--   2. Restoring a database to a new cluster silently lost ROLE ATTRIBUTES --
--      they are cluster-level and live in no database dump. `service_role` came
--      back without BYPASSRLS, which does not fail: it quietly filters every
--      server-side admin query through RLS and returns fewer rows.
--
-- Both are properties of the CATALOG, so the catalog is what gets asserted.
--
-- Run:  psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f test_database_posture.sql
-- Exits non-zero on any failure, so it can gate a deploy.

\set ON_ERROR_STOP on
\pset tuples_only off

CREATE TEMP TABLE posture_result(check_name text, ok boolean, detail text);

CREATE OR REPLACE FUNCTION pg_temp.expect(name text, cond boolean, detail text)
RETURNS void LANGUAGE plpgsql AS $$
BEGIN
    INSERT INTO posture_result VALUES (name, cond, detail);
END;
$$;

-- ---------------------------------------------------------------------------
-- 1. Every table holding per-user rows has RLS ENABLED **and FORCED**.
--
--    FORCE is the half that is easy to miss and easy to get wrong: without it
--    the table OWNER is exempt, and the owner is who migrations and the SQL
--    editor run as. Enabled-but-not-forced reads as protection in \d and is not.
-- ---------------------------------------------------------------------------
DO $$
DECLARE
    r record;
    -- inference_jobs is a SHARED WORK QUEUE, not per-user data, and RLS there
    -- would break both assistants for no confidentiality gain. Named here so
    -- the exemption is a decision on the record rather than a silent absence.
    exempt text[] := ARRAY['inference_jobs'];
    -- Guard tables carry NO grants to any application role and are reached only
    -- by a SECURITY DEFINER function running as the owner. RLS is enabled on
    -- them, but FORCE would bind the owner and break that function.
    enabled_only text[] := ARRAY['protected_relations','data_hazard_install_log'];
BEGIN
    FOR r IN
        SELECT c.relname, c.relrowsecurity AS rls, c.relforcerowsecurity AS forced,
               (SELECT count(*) FROM pg_policies p
                 WHERE p.schemaname='public' AND p.tablename=c.relname) AS policies
          FROM pg_class c
         WHERE c.relnamespace='public'::regnamespace AND c.relkind='r'
         ORDER BY c.relname
    LOOP
        IF r.relname = ANY(exempt) THEN
            CONTINUE;
        ELSIF r.relname = ANY(enabled_only) THEN
            PERFORM pg_temp.expect(
                format('%s: RLS enabled (guard table, no grants, fail-closed)', r.relname),
                r.rls,
                format('rls=%s policies=%s', r.rls, r.policies));
        ELSE
            PERFORM pg_temp.expect(
                format('%s: RLS enabled AND forced, with a policy', r.relname),
                r.rls AND r.forced AND r.policies > 0,
                format('rls=%s force=%s policies=%s', r.rls, r.forced, r.policies));
        END IF;
    END LOOP;
END;
$$;

-- ---------------------------------------------------------------------------
-- 2. No application role is a superuser or holds BYPASSRLS.
--
--    Postgres always bypasses row security for a superuser or a BYPASSRLS role,
--    so a policy in front of one filters nothing at all. `postgres` is expected
--    to hold both; `service_role` is expected to hold BYPASSRLS, because that is
--    what Supabase's service key is FOR. Every other role must hold neither, and
--    the list is explicit so a NEW role cannot arrive unnoticed.
-- ---------------------------------------------------------------------------
DO $$
DECLARE
    r record;
BEGIN
    FOR r IN
        SELECT rolname, rolsuper, rolbypassrls FROM pg_roles
         WHERE rolname NOT LIKE 'pg\_%' ORDER BY rolname
    LOOP
        IF r.rolname = 'postgres' THEN
            PERFORM pg_temp.expect('postgres is the superuser (expected)',
                r.rolsuper, format('super=%s', r.rolsuper));
        ELSIF r.rolname = 'service_role' THEN
            -- Asserted PRESENT, not absent: this attribute is cluster-level and
            -- is silently lost by a dump/restore to a new cluster, which does
            -- not fail -- it just returns fewer rows.
            PERFORM pg_temp.expect('service_role retains BYPASSRLS after any restore',
                r.rolbypassrls, format('bypassrls=%s', r.rolbypassrls));
        ELSE
            PERFORM pg_temp.expect(
                format('%s is neither superuser nor BYPASSRLS', r.rolname),
                NOT r.rolsuper AND NOT r.rolbypassrls,
                format('super=%s bypassrls=%s', r.rolsuper, r.rolbypassrls));
        END IF;
    END LOOP;
END;
$$;

-- ---------------------------------------------------------------------------
-- 3. Transport: TLS on, and no non-approved cipher reachable.
--
--    This is an ALGORITHM statement, not a FIPS claim. Reading a provider name
--    is not a certificate, and this repository refuses to make compliance
--    claims it cannot substantiate -- see fips_mode.cppm. What is asserted is
--    exactly what is true: TLS is on, the connection is TLS 1.2 or better, and
--    3DES/RC4/MD5/DES/export suites are excluded.
-- ---------------------------------------------------------------------------
DO $$
DECLARE
    v_ssl text;    v_ciphers text;   v_min text;
    c_ssl boolean; c_ver text;       c_cipher text;
BEGIN
    SELECT setting INTO v_ssl      FROM pg_settings WHERE name='ssl';
    SELECT setting INTO v_ciphers  FROM pg_settings WHERE name='ssl_ciphers';
    SELECT setting INTO v_min      FROM pg_settings WHERE name='ssl_min_protocol_version';
    SELECT ssl, version, cipher INTO c_ssl, c_ver, c_cipher
      FROM pg_stat_ssl WHERE pid = pg_backend_pid();

    PERFORM pg_temp.expect('server has TLS enabled', v_ssl = 'on', 'ssl='||v_ssl);
    PERFORM pg_temp.expect('minimum protocol is TLSv1.2 or better',
        v_min IN ('TLSv1.2','TLSv1.3'), 'min='||v_min);
    PERFORM pg_temp.expect('3DES is not offered',
        position('!3DES' in v_ciphers) > 0, 'ciphers='||v_ciphers);
    PERFORM pg_temp.expect('RC4/MD5/DES/export suites are not offered',
        position('!RC4' in v_ciphers) > 0 AND position('!MD5' in v_ciphers) > 0
        AND position('!DES' in v_ciphers) > 0 AND position('!EXPORT' in v_ciphers) > 0,
        'ciphers='||v_ciphers);
    -- A local socket connection legitimately has no TLS; only assert the
    -- negotiated suite when this session actually came over TCP+TLS.
    IF c_ssl THEN
        PERFORM pg_temp.expect('this session negotiated TLS 1.2+ with an AEAD cipher',
            c_ver IN ('TLSv1.2','TLSv1.3') AND (c_cipher LIKE '%GCM%' OR c_cipher LIKE '%CHACHA%'),
            format('%s / %s', c_ver, c_cipher));
    END IF;
END;
$$;

-- ---------------------------------------------------------------------------
-- 4. The data-hazard guards are armed, and armed the way they must be.
--
--    `guard_mass_delete` being SECURITY DEFINER is asserted BY NAME because
--    SECURITY INVOKER is not a degraded guard -- it is a guard that breaks every
--    ordinary DELETE, and it shipped that way once.
-- ---------------------------------------------------------------------------
DO $$
DECLARE
    n_protected int; n_evt int; secdef boolean; cfg text[]; n_trg int;
BEGIN
    SELECT count(*) INTO n_protected FROM public.protected_relations;
    SELECT count(*) INTO n_evt FROM pg_event_trigger
     WHERE evtname IN ('guard_protected_drop_trg','guard_protected_schema_trg');
    SELECT prosecdef, proconfig INTO secdef, cfg FROM pg_proc WHERE proname='guard_mass_delete';
    SELECT count(*) INTO n_trg FROM pg_trigger t
      WHERE NOT t.tgisinternal AND t.tgname LIKE 'guard\_%';

    PERFORM pg_temp.expect('the guard list is populated', n_protected > 0,
        format('%s protected relations', n_protected));
    PERFORM pg_temp.expect('the DROP event trigger is installed', n_evt >= 1,
        format('%s event trigger(s)', n_evt));
    PERFORM pg_temp.expect('guard_mass_delete is SECURITY DEFINER', coalesce(secdef,false),
        format('prosecdef=%s', secdef));
    PERFORM pg_temp.expect('guard_mass_delete pins its search_path',
        cfg IS NOT NULL AND array_to_string(cfg,',') LIKE '%search_path%',
        format('proconfig=%s', coalesce(array_to_string(cfg,','),'<none>')));
    PERFORM pg_temp.expect('DML guard triggers are attached', n_trg > 0,
        format('%s trigger(s)', n_trg));
END;
$$;

-- ---------------------------------------------------------------------------
-- Report, and FAIL THE PROCESS if anything is red.
-- ---------------------------------------------------------------------------
\echo ''
\echo '=== database security posture ==='
SELECT CASE WHEN ok THEN '  PASS' ELSE '  FAIL' END AS result,
       check_name, detail
  FROM posture_result ORDER BY ok, check_name;

SELECT count(*) FILTER (WHERE ok) AS passed,
       count(*) FILTER (WHERE NOT ok) AS failed,
       count(*) AS checks
  FROM posture_result;

DO $$
DECLARE n int;
BEGIN
    SELECT count(*) INTO n FROM posture_result WHERE NOT ok;
    IF n > 0 THEN
        RAISE EXCEPTION 'database security posture: % check(s) FAILED', n;
    END IF;
END;
$$;
