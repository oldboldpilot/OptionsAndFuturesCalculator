-- 08_data_hazard_guards.sql
--
-- @author Olumuyiwa Oluwasanmi
--
-- Guards against table loss and mass row loss on the Railway database behind
-- optionsandfuturescalculator.com.
--
-- WHY NEITHER RLS NOR GRANT CAN DO THIS JOB, WHICH IS THE WHOLE POINT.
--
-- Migration 04 measured the thing that decides this file:
--
--   SELECT rolsuper, rolbypassrls FROM pg_roles WHERE rolname = current_user;
--   -> t | t
--
-- The engine connects as `postgres`, a superuser with rolbypassrls. Migration
-- 04 worked around that for TENANCY by dropping into `ofc_app` per transaction,
-- because the thing being protected was one caller's rows from another caller.
-- That mechanism is useless here. The hazard this file addresses is not a
-- caller -- it is a DROP TABLE typed into a psql session, a migration with the
-- wrong table name, or a DELETE whose WHERE clause was edited away. Every one
-- of those arrives AS the superuser, and a superuser is exempt from RLS, from
-- FORCE ROW LEVEL SECURITY, and from every REVOKE.
--
-- Postgres has exactly one mechanism that binds a superuser: EVENT TRIGGERS.
-- They fire for every role including superusers, and raising inside one aborts
-- the surrounding transaction. That is why this file is built out of event
-- triggers and statement triggers rather than out of privileges.
--
-- THERE IS AN ESCAPE HATCH AND IT IS LOAD-BEARING. A guard with no legitimate
-- way past it is removed the first time somebody legitimately needs to drop a
-- table, and then it protects nothing. `app.allow_destructive` opens the gate
-- for one transaction:
--
--   BEGIN;
--   SET LOCAL app.allow_destructive = 'on';
--   DROP TABLE public.whatever;
--   COMMIT;
--
-- SET LOCAL is deliberate: the setting dies with the transaction, so a pooled
-- connection is never handed on with the guard disarmed. That is the same
-- reasoning migration 04 applies to `set_config(..., is_local => true)`.
--
-- WHAT THIS DOES NOT DO. It does not make the data recoverable. A guard stops
-- the accidents it anticipates and says nothing about the ones it does not --
-- a corrupt volume, a dropped Railway service, a ransomware event. That needs a
-- restorable dump, which is scripts/backup_database_to_nas.sh, added alongside
-- this file. Prevention and recovery are different properties and this file
-- only supplies the first.

BEGIN;

-- ---------------------------------------------------------------------------
-- 1. The protected set, as DATA rather than as a list repeated in three
--    functions. A list maintained by hand in several places is the drift
--    problem this project has already paid for in another repository, where a
--    hand-written operation list refused thirteen of twenty-seven operations.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.protected_relations (
    schema_name        text    NOT NULL,
    table_name         text    NOT NULL,
    block_drop         boolean NOT NULL DEFAULT true,
    block_truncate     boolean NOT NULL DEFAULT true,
    -- A statement deleting more than `mass_delete_fraction` of the table AND
    -- more than `mass_delete_floor` rows is refused. NULL disables the check.
    -- Two conditions rather than one: a fraction alone refuses an ordinary
    -- delete on a table holding three rows, and a row count alone lets
    -- `DELETE FROM saved_strategies` through once the table is small.
    mass_delete_fraction numeric,
    mass_delete_floor    integer,
    reason             text    NOT NULL,
    PRIMARY KEY (schema_name, table_name)
);

COMMENT ON TABLE public.protected_relations IS
    'Tables guarded by migration 08. Rows here are read by the event trigger and '
    'by the truncate/delete triggers -- one list, three consumers.';

INSERT INTO public.protected_relations
    (schema_name, table_name, block_drop, block_truncate,
     mass_delete_fraction, mass_delete_floor, reason)
VALUES
    ('public', 'saved_strategies', true,  true,  0.50, 25,
     'user-authored positions; unrecoverable, and the FK from auth.users already cascades'),
    ('public', 'users',            true,  true,  0.50, 25,
     'account rows'),
    ('public', 'profiles',         true,  true,  0.50, 25,
     'account rows'),
    ('public', 'state_assumptions', true, true,  0.50, 25,
     'fifty Census rows plus hand-authored editorial columns migration 07 makes unwritable by the refresh role'),
    ('public', 'job_runs',         true,  true,  NULL, NULL,
     'refresh audit trail; append-only in practice, and a staleness check that lies is the LIVE-badge defect again'),
    -- inference_jobs is DROP- and TRUNCATE-protected but carries NO mass-delete
    -- guard, and that asymmetry is deliberate: it is a work queue whose whole
    -- lifecycle is bulk removal of completed rows. A guard there would refuse
    -- ordinary maintenance, be switched off, and take the drop protection with
    -- it.
    ('public', 'inference_jobs',   true,  false, NULL, NULL,
     'shared work queue: drop-protected, but TRUNCATE is how its own test suite resets it'),
    ('auth',   'users',            true,  true,  0.50, 25,
     'GoTrue identities; migration 05 cascades saved_strategies off this table, so losing it loses those too')
ON CONFLICT (schema_name, table_name) DO UPDATE
    SET block_drop           = EXCLUDED.block_drop,
        block_truncate       = EXCLUDED.block_truncate,
        mass_delete_fraction = EXCLUDED.mass_delete_fraction,
        mass_delete_floor    = EXCLUDED.mass_delete_floor,
        reason               = EXCLUDED.reason;

-- ---------------------------------------------------------------------------
-- 2. DROP protection, via an event trigger -- the only thing a superuser
--    cannot walk past.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION public.guard_protected_drop() RETURNS event_trigger
LANGUAGE plpgsql AS $$
DECLARE
    obj    record;
    reason text;
BEGIN
    IF current_setting('app.allow_destructive', true) = 'on' THEN
        RETURN;
    END IF;
    FOR obj IN SELECT * FROM pg_event_trigger_dropped_objects() LOOP
        IF obj.object_type IN ('table', 'view', 'materialized view') THEN
            -- THE GUARD LIST IS CHECKED BY NAME, BEFORE IT IS QUERIED. At
            -- `sql_drop` time the catalogue change has already happened, so on
            -- `DROP TABLE protected_relations` the SELECT below would raise
            -- 42P01 rather than the refusal. That still rolls the drop back --
            -- measured -- but it is an accident, not a control: the day someone
            -- makes this function tolerant of a missing list is the day the list
            -- becomes droppable. Refuse it on its own name instead.
            IF obj.schema_name = 'public' AND obj.object_name = 'protected_relations' THEN
                RAISE EXCEPTION
                    'refusing to drop public.protected_relations: it is the guard list '
                    'for migration 08, and dropping it disarms every check'
                USING HINT = 'BEGIN; SET LOCAL app.allow_destructive = ''on''; DROP ...; COMMIT;',
                      ERRCODE = 'insufficient_privilege';
            END IF;
            SELECT p.reason INTO reason
              FROM public.protected_relations p
             WHERE p.schema_name = obj.schema_name
               AND p.table_name  = obj.object_name
               AND p.block_drop;
            IF FOUND THEN
                RAISE EXCEPTION
                    'refusing to drop protected relation %.%: %',
                    obj.schema_name, obj.object_name, reason
                USING HINT = 'if this is deliberate: BEGIN; SET LOCAL app.allow_destructive = ''on''; <your DDL>; COMMIT;',
                      ERRCODE = 'insufficient_privilege';
            END IF;
        END IF;
    END LOOP;
END;
$$;

-- `sql_drop` rather than `ddl_command_start`, because only sql_drop can say
-- WHICH relation is going. ddl_command_start fires before the catalogue knows,
-- so a guard written there would have to parse the statement text -- and a
-- guard that reads SQL with a regex is a guard with a bypass.
DROP EVENT TRIGGER IF EXISTS guard_protected_drop_trg;
CREATE EVENT TRIGGER guard_protected_drop_trg
    ON sql_drop EXECUTE FUNCTION public.guard_protected_drop();

-- DROP SCHEMA public CASCADE removes the protected_relations table itself in
-- the same statement, so the row lookup above can race its own evidence. Refuse
-- the schema outright, before anything is dropped.
CREATE OR REPLACE FUNCTION public.guard_protected_schema() RETURNS event_trigger
LANGUAGE plpgsql AS $$
BEGIN
    IF current_setting('app.allow_destructive', true) = 'on' THEN
        RETURN;
    END IF;
    IF tg_tag IN ('DROP SCHEMA') THEN
        RAISE EXCEPTION 'refusing DROP SCHEMA while data-hazard guards are armed'
        USING HINT = 'BEGIN; SET LOCAL app.allow_destructive = ''on''; ...; COMMIT;',
              ERRCODE = 'insufficient_privilege';
    END IF;
END;
$$;

DROP EVENT TRIGGER IF EXISTS guard_protected_schema_trg;
CREATE EVENT TRIGGER guard_protected_schema_trg
    ON ddl_command_start
    WHEN TAG IN ('DROP SCHEMA')
    EXECUTE FUNCTION public.guard_protected_schema();

-- ---------------------------------------------------------------------------
-- 3. TRUNCATE protection. Event triggers do NOT fire on TRUNCATE -- it is DML,
--    not DDL -- so this needs a per-table statement trigger. Those also bind a
--    superuser.
-- ---------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION public.guard_protected_truncate() RETURNS trigger
LANGUAGE plpgsql AS $$
BEGIN
    IF current_setting('app.allow_destructive', true) = 'on' THEN
        RETURN NULL;
    END IF;
    RAISE EXCEPTION 'refusing to TRUNCATE protected table %.%',
        tg_table_schema, tg_table_name
    USING HINT = 'BEGIN; SET LOCAL app.allow_destructive = ''on''; TRUNCATE ...; COMMIT;',
          ERRCODE = 'insufficient_privilege';
END;
$$;

-- ---------------------------------------------------------------------------
-- 4. Mass-delete protection, using a transition table so the trigger can COUNT
--    what a statement removed. A BEFORE DELETE ... FOR EACH STATEMENT trigger
--    cannot see the rows and a FOR EACH ROW trigger cannot see the total, so
--    neither could state this rule at all.
-- ---------------------------------------------------------------------------
-- SECURITY DEFINER is load-bearing TWICE, and a GRANT would only fix the first.
--
--   1. The function reads public.protected_relations, and a DML trigger runs as
--      the INVOKING role. Every application delete arrives as ofc_app, which has
--      no SELECT on the guard list, so DeleteStrategy failed `permission denied
--      for table protected_relations` for every user -- caught by section 4 of
--      test_strategy_store_pg, 8 failures, all one cause.
--
--   2. `remaining` is counted through RLS otherwise. As ofc_app the count sees
--      only the CALLER'S rows, so the fraction is measured against one subject's
--      slice instead of the table: a user deleting 30 of their own rows trips
--      `30 > 0.50 * 30` while the table holds thousands. A mass-delete rule is a
--      statement about the TABLE, so its denominator has to be the table.
--
-- search_path is pinned because a SECURITY DEFINER function that resolves names
-- through the caller's path lets the caller choose which protected_relations it
-- reads. The transition table `deleted_rows` is an ephemeral named relation
-- resolved ahead of search_path, so pinning does not hide it.
CREATE OR REPLACE FUNCTION public.guard_mass_delete() RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
DECLARE
    removed   bigint;
    remaining bigint;
    frac      numeric;
    floor_n   integer;
BEGIN
    IF current_setting('app.allow_destructive', true) = 'on' THEN
        RETURN NULL;
    END IF;
    SELECT p.mass_delete_fraction, p.mass_delete_floor INTO frac, floor_n
      FROM public.protected_relations p
     WHERE p.schema_name = tg_table_schema AND p.table_name = tg_table_name;
    IF frac IS NULL OR floor_n IS NULL THEN
        RETURN NULL;
    END IF;
    SELECT count(*) INTO removed FROM deleted_rows;
    IF removed <= floor_n THEN
        RETURN NULL;
    END IF;
    EXECUTE format('SELECT count(*) FROM %I.%I', tg_table_schema, tg_table_name)
       INTO remaining;
    -- `removed + remaining` is the size the table had BEFORE the statement:
    -- this trigger is AFTER, so the rows are already gone from the count.
    IF removed::numeric > frac * (removed + remaining)::numeric THEN
        RAISE EXCEPTION
            'refusing to delete % of % rows from %.% in one statement',
            removed, removed + remaining, tg_table_schema, tg_table_name
        USING HINT = 'delete in smaller batches, or BEGIN; SET LOCAL app.allow_destructive = ''on''; ...; COMMIT;',
              ERRCODE = 'insufficient_privilege';
    END IF;
    RETURN NULL;
END;
$$;

-- ---------------------------------------------------------------------------
-- 5. Attach the two DML triggers to every row of the protected set. Generated
--    from the table rather than written out, so adding a protected relation is
--    one INSERT and a re-run of this block -- there is no second list to forget.
-- ---------------------------------------------------------------------------
DO $$
DECLARE
    r record;
BEGIN
    FOR r IN SELECT * FROM public.protected_relations LOOP
        IF to_regclass(format('%I.%I', r.schema_name, r.table_name)) IS NULL THEN
            RAISE NOTICE 'skipping %.% -- not present in this database',
                r.schema_name, r.table_name;
            CONTINUE;
        END IF;
        EXECUTE format('DROP TRIGGER IF EXISTS guard_truncate_trg ON %I.%I',
                       r.schema_name, r.table_name);
        IF r.block_truncate THEN
            EXECUTE format(
                'CREATE TRIGGER guard_truncate_trg BEFORE TRUNCATE ON %I.%I '
                'FOR EACH STATEMENT EXECUTE FUNCTION public.guard_protected_truncate()',
                r.schema_name, r.table_name);
        END IF;
        EXECUTE format('DROP TRIGGER IF EXISTS guard_mass_delete_trg ON %I.%I',
                       r.schema_name, r.table_name);
        IF r.mass_delete_fraction IS NOT NULL THEN
            EXECUTE format(
                'CREATE TRIGGER guard_mass_delete_trg AFTER DELETE ON %I.%I '
                'REFERENCING OLD TABLE AS deleted_rows '
                'FOR EACH STATEMENT EXECUTE FUNCTION public.guard_mass_delete()',
                r.schema_name, r.table_name);
        END IF;
    END LOOP;
END;
$$;

-- ---------------------------------------------------------------------------
-- 6. The guard table guards itself. Emptying protected_relations disarms every
--    check above in one statement, silently, and nothing else here would notice.
-- ---------------------------------------------------------------------------
DROP TRIGGER IF EXISTS guard_truncate_trg ON public.protected_relations;
CREATE TRIGGER guard_truncate_trg BEFORE TRUNCATE ON public.protected_relations
    FOR EACH STATEMENT EXECUTE FUNCTION public.guard_protected_truncate();

INSERT INTO public.protected_relations
    (schema_name, table_name, block_drop, block_truncate,
     mass_delete_fraction, mass_delete_floor, reason)
VALUES ('public', 'protected_relations', true, true, NULL, NULL,
        'the guard list itself: dropping it disarms every check in migration 08')
ON CONFLICT (schema_name, table_name) DO NOTHING;

-- ---------------------------------------------------------------------------
-- The guard tables guard themselves at the ROW level too.
--
-- These carry no grants to anon/authenticated/service_role/ofc_app, so nothing
-- could read them already. RLS with ZERO policies is added anyway, because the
-- posture is now ASSERTED by a regression test that sweeps every table in the
-- schema, and "this one is safe for a different reason" is exactly the kind of
-- exception that rots. Fail-closed by construction: no policy means no row is
-- visible to any role that is neither the owner nor a superuser.
--
-- ENABLE, deliberately NOT FORCE. `guard_mass_delete()` is SECURITY DEFINER and
-- runs as this table's owner; FORCE would bind the owner too, so a deployment
-- whose owner is not a superuser would have its own guard unable to read the
-- guard list -- reintroducing the exact defect SECURITY DEFINER was added to
-- fix, one layer down.
-- ---------------------------------------------------------------------------
DO $guard_rls$
DECLARE
    t text;
BEGIN
    FOREACH t IN ARRAY ARRAY['protected_relations',
                             'data_hazard_install_log']
    LOOP
        IF to_regclass(format('public.%I', t)) IS NOT NULL THEN
            EXECUTE format('ALTER TABLE public.%I ENABLE ROW LEVEL SECURITY', t);
            EXECUTE format('REVOKE ALL ON public.%I FROM PUBLIC', t);
        END IF;
    END LOOP;
END;
$guard_rls$;

COMMIT;
