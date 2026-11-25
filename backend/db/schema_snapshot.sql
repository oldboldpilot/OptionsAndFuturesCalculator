-- Deterministic text snapshot of everything the migrations are responsible for.
--
-- @author Olumuyiwa Oluwasanmi
--
-- One line per catalog fact:  kind <TAB> key <TAB> detail.  Two databases that
-- hold the same schema print byte-identical output, so the drift gate
-- (scripts/check_schema_drift.sh) is a plain diff of two of these.
--
-- WHAT IS IN SCOPE, AND WHY. Everything a hand-edit could change without any
-- application behaviour noticing: columns, constraints, indexes, triggers,
-- functions, event triggers, ROW-LEVEL-SECURITY flags and POLICIES, grants,
-- default privileges, enums, extensions, and the attributes of the roles this
-- project owns. RLS and roles are first-class here because dropping a policy
-- changes no answer the application returns (see test_database_posture.sql).
--
-- WHAT IS DELIBERATELY OUT OF SCOPE.
--   * auth.* except the eight auth.users token columns migration 09 repairs and
--     the guard triggers migration 08 attaches to auth tables:
--     GoTrue owns that schema and migrates it itself (auth.schema_migrations).
--     Comparing it would make every GoTrue upgrade a false drift.
--   * ofc_meta.*: the migration runner's own ledger (outside public on purpose).
--   * Row data, except public.protected_relations, whose rows ARE the armed
--     state of migration 08's guards.
--   * Object owners other than "a superuser" vs a named role, and role
--     passwords/LOGIN secrets: they differ between a scratch database and
--     the hosted one and are not schema.
--   * Extension versions: they move with the server image, not the schema.
--
-- search_path is pinned so every pg_get_*def prints fully-qualified names.
SET search_path = pg_catalog;
SET client_min_messages = warning;

WITH
roles_of_interest AS (
    SELECT r.oid, r.rolname FROM pg_roles r
     WHERE r.rolname !~ '^pg_' AND r.rolname <> 'postgres'   -- the bootstrap superuser only; any OTHER superuser is a fact
),
rel AS (
    SELECT c.oid, c.relname, c.relkind, c.relrowsecurity, c.relforcerowsecurity,
           c.relpersistence, c.reloptions, c.relacl, c.relowner
      FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
     WHERE n.nspname = 'public'
       AND c.relkind IN ('r','p','v','m','S','f')
       AND c.relname <> 'schema_migrations'
       AND NOT EXISTS (SELECT 1 FROM pg_depend d
                        WHERE d.objid = c.oid AND d.deptype = 'e')
       AND NOT EXISTS (SELECT 1 FROM pg_depend d  -- identity/serial sequences print with their column
                        WHERE d.objid = c.oid AND c.relkind = 'S' AND d.deptype IN ('a','i'))
),
owner_name AS (
    SELECT oid, CASE WHEN rolsuper THEN '<superuser>' ELSE rolname END AS nm FROM pg_roles
),
acl AS (  -- relation-level grants, owner's own implicit grants dropped
    SELECT 'grant' AS kind, 'rel ' || r.relname AS key,
           coalesce(g.rolname, 'PUBLIC') || ' ' || a.privilege_type
           || CASE WHEN a.is_grantable THEN ' WITH GRANT OPTION' ELSE '' END AS detail
      FROM rel r, LATERAL aclexplode(r.relacl) a
      LEFT JOIN pg_roles g ON g.oid = a.grantee
     WHERE a.grantee <> r.relowner
),
colacl AS (
    SELECT 'grant', 'col ' || r.relname || '.' || att.attname,
           coalesce(g.rolname, 'PUBLIC') || ' ' || a.privilege_type
      FROM rel r
      JOIN pg_attribute att ON att.attrelid = r.oid AND att.attacl IS NOT NULL AND NOT att.attisdropped,
           LATERAL aclexplode(att.attacl) a
      LEFT JOIN pg_roles g ON g.oid = a.grantee
),
nsacl AS (
    SELECT 'grant', 'schema ' || n.nspname,
           coalesce(g.rolname, 'PUBLIC') || ' ' || a.privilege_type
      FROM pg_namespace n, LATERAL aclexplode(n.nspacl) a
      LEFT JOIN pg_roles g ON g.oid = a.grantee
     WHERE n.nspname = 'public' AND a.grantee <> n.nspowner
),
fn AS (
    SELECT p.oid, p.proname, p.proowner, p.proacl, p.prosecdef, p.proconfig, p.prokind
      FROM pg_proc p JOIN pg_namespace n ON n.oid = p.pronamespace
     WHERE n.nspname = 'public' AND p.prokind IN ('f','p')
       AND NOT EXISTS (SELECT 1 FROM pg_depend d WHERE d.objid = p.oid AND d.deptype = 'e')
),
fnacl AS (
    SELECT 'grant', 'fn ' || f.oid::regprocedure::text,
           coalesce(g.rolname, 'PUBLIC') || ' ' || a.privilege_type
      FROM fn f, LATERAL aclexplode(f.proacl) a
      LEFT JOIN pg_roles g ON g.oid = a.grantee
     WHERE a.grantee <> f.proowner
),
auth_cols AS (
    SELECT a.attname, a.attnotnull, pg_get_expr(d.adbin, d.adrelid) AS dflt,
           format_type(a.atttypid, a.atttypmod) AS typ
      FROM pg_attribute a
      LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
     WHERE a.attrelid = to_regclass('auth.users') AND NOT a.attisdropped
       AND a.attname IN ('confirmation_token','recovery_token','email_change',
                         'email_change_token_new','email_change_token_current',
                         'phone_change','phone_change_token','reauthentication_token')
),
lines(kind, key, detail) AS (
    -- The first branch fixes every column's type to text. Without it UNION resolves
    -- detail to `name` (from role_member) and silently TRUNCATES every line at
    -- 63 bytes: a role's bypassrls flag sat past the cut and the gate was blind.
    SELECT 'extension'::text, e.extname::text, ''::text FROM pg_extension e
    UNION ALL
    SELECT 'schema', n.nspname, coalesce(o.nm, '?')
      FROM pg_namespace n JOIN owner_name o ON o.oid = n.nspowner
     WHERE n.nspname = 'public'
    UNION ALL
    SELECT 'relation', r.relname,
           r.relkind::text || ' rls=' || r.relrowsecurity || ' force=' || r.relforcerowsecurity
           || ' persist=' || r.relpersistence::text || ' owner=' || o.nm
           || ' opts=' || coalesce(r.reloptions::text, '')
      FROM rel r JOIN owner_name o ON o.oid = r.relowner
    UNION ALL
    SELECT 'column', r.relname || '.' || a.attname,
           format_type(a.atttypid, a.atttypmod)
           || CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END
           || CASE WHEN a.attidentity <> '' THEN ' IDENTITY ' || a.attidentity::text ELSE '' END
           || CASE WHEN a.attgenerated <> '' THEN ' GENERATED ' || a.attgenerated::text ELSE '' END
           || coalesce(' DEFAULT ' || pg_get_expr(d.adbin, d.adrelid), '')
           || coalesce(' COLLATE ' || nullif(co.collname, 'default'), '')
      FROM rel r
      JOIN pg_attribute a ON a.attrelid = r.oid AND a.attnum > 0 AND NOT a.attisdropped
      LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum
      LEFT JOIN pg_collation co ON co.oid = a.attcollation AND a.attcollation <> 0
     WHERE r.relkind IN ('r','p','f','v','m')
    UNION ALL
    SELECT 'constraint', r.relname || ' ' || c.conname,
           pg_get_constraintdef(c.oid) || CASE WHEN c.convalidated THEN '' ELSE ' NOT VALIDATED' END
      FROM rel r JOIN pg_constraint c ON c.conrelid = r.oid
    UNION ALL
    SELECT 'index', i.tablename || ' ' || i.indexname, i.indexdef
      FROM pg_indexes i
     WHERE i.schemaname = 'public' AND i.tablename <> 'schema_migrations'
       AND NOT EXISTS (SELECT 1 FROM pg_depend d JOIN pg_class ic ON ic.oid = d.objid
                        WHERE ic.relname = i.indexname AND d.deptype = 'e')
    UNION ALL
    SELECT 'trigger', r.relname || ' ' || t.tgname,
           pg_get_triggerdef(t.oid) || ' enabled=' || t.tgenabled::text
      FROM rel r JOIN pg_trigger t ON t.tgrelid = r.oid AND NOT t.tgisinternal
    UNION ALL
    SELECT 'policy', r.relname || ' ' || p.polname,
           (CASE WHEN p.polpermissive THEN 'PERMISSIVE' ELSE 'RESTRICTIVE' END)
           || ' ' || p.polcmd::text
           || ' TO ' || coalesce((SELECT string_agg(coalesce(g.rolname, 'PUBLIC'), ',' ORDER BY coalesce(g.rolname, 'PUBLIC'))
                                    FROM unnest(p.polroles) u(oid) LEFT JOIN pg_roles g ON g.oid = u.oid), '')
           || ' USING (' || coalesce(pg_get_expr(p.polqual, p.polrelid), '') || ')'
           || ' CHECK (' || coalesce(pg_get_expr(p.polwithcheck, p.polrelid), '') || ')'
      FROM rel r JOIN pg_policy p ON p.polrelid = r.oid
    UNION ALL
    SELECT 'function', f.oid::regprocedure::text,
           pg_get_functiondef(f.oid) || ' secdef=' || f.prosecdef
           || ' config=' || coalesce(f.proconfig::text, '') || ' owner=' || o.nm
      FROM fn f JOIN owner_name o ON o.oid = f.proowner
    UNION ALL
    SELECT 'event_trigger', et.evtname,
           et.evtevent || ' tags=' || coalesce(et.evttags::text, '') || ' fn='
           || et.evtfoid::regproc::text || ' enabled=' || et.evtenabled::text
      FROM pg_event_trigger et
    UNION ALL
    SELECT 'enum', t.typname,
           (SELECT string_agg(e.enumlabel, ',' ORDER BY e.enumsortorder) FROM pg_enum e WHERE e.enumtypid = t.oid)
      FROM pg_type t JOIN pg_namespace n ON n.oid = t.typnamespace
     WHERE n.nspname = 'public' AND t.typtype = 'e'
    UNION ALL
    SELECT 'sequence', r.relname,
           format_type(s.seqtypid, NULL) || ' start=' || s.seqstart || ' inc=' || s.seqincrement
           || ' min=' || s.seqmin || ' max=' || s.seqmax || ' cache=' || s.seqcache || ' cycle=' || s.seqcycle
      FROM rel r JOIN pg_sequence s ON s.seqrelid = r.oid
     WHERE r.relkind = 'S'
    UNION ALL
    SELECT 'role', ro.rolname,
           'superuser=' || r.rolsuper || ' inherit=' || r.rolinherit || ' createrole=' || r.rolcreaterole || ' createdb=' || r.rolcreatedb
           || ' login=' || r.rolcanlogin || ' bypassrls=' || r.rolbypassrls || ' replication=' || r.rolreplication
           || ' connlimit=' || r.rolconnlimit
      FROM roles_of_interest ro JOIN pg_roles r ON r.oid = ro.oid
    UNION ALL
    SELECT 'role_member', ro.rolname, m.rolname
      FROM pg_auth_members am
      JOIN roles_of_interest ro ON ro.oid = am.member
      JOIN pg_roles m ON m.oid = am.roleid
     WHERE m.rolname !~ '^pg_'
    UNION ALL
    SELECT 'role_config', ro.rolname, c
      FROM roles_of_interest ro
      JOIN pg_db_role_setting s ON s.setrole = ro.oid AND s.setdatabase = 0,
           unnest(s.setconfig) c
    UNION ALL
    SELECT 'default_acl', coalesce(g.rolname, '?') || ' ' || d.defaclobjtype::text, d.defaclacl::text
      FROM pg_default_acl d JOIN pg_namespace n ON n.oid = d.defaclnamespace
      LEFT JOIN pg_roles g ON g.oid = d.defaclrole
     WHERE n.nspname = 'public'
    UNION ALL
    SELECT 'comment', 'rel ' || r.relname, obj_description(r.oid, 'pg_class')
      FROM rel r WHERE obj_description(r.oid, 'pg_class') IS NOT NULL
    UNION ALL
    SELECT 'comment', 'fn ' || f.oid::regprocedure::text, obj_description(f.oid, 'pg_proc')
      FROM fn f WHERE obj_description(f.oid, 'pg_proc') IS NOT NULL
    UNION ALL SELECT * FROM acl
    UNION ALL SELECT * FROM colacl
    UNION ALL SELECT * FROM nsacl
    UNION ALL SELECT * FROM fnacl
    UNION ALL  -- OFC's guard triggers live ON GoTrue's table but call OFC's functions
    SELECT 'auth_trigger', t.tgrelid::regclass::text || ' ' || t.tgname,
           pg_get_triggerdef(t.oid) || ' enabled=' || t.tgenabled::text
      FROM pg_trigger t JOIN pg_proc p ON p.oid = t.tgfoid
      JOIN pg_namespace pn ON pn.oid = p.pronamespace
     WHERE NOT t.tgisinternal AND pn.nspname = 'public'
       AND t.tgrelid IN (SELECT c.oid FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace
                          WHERE n.nspname = 'auth')
    UNION ALL
    SELECT 'auth_column', 'auth.users.' || attname,
           typ || CASE WHEN attnotnull THEN ' NOT NULL' ELSE '' END || coalesce(' DEFAULT ' || dflt, '')
      FROM auth_cols
)
SELECT line FROM (SELECT replace(replace(kind || E'\t' || key || E'\t' || coalesce(detail, ''), E'\n', E'\\n'), E'\r', '') AS line
  FROM lines) q
 ORDER BY line COLLATE "C";

-- The armed state of migration 08: its rows are configuration, not data.
SELECT to_regclass('public.protected_relations') IS NOT NULL AS has_pr \gset
\if :has_pr
SELECT 'data' || E'\t' || 'protected_relations ' || schema_name || '.' || table_name
       || E'\t' || 'drop=' || block_drop || ' truncate=' || block_truncate
       || ' frac=' || mass_delete_fraction || ' floor=' || mass_delete_floor
  FROM public.protected_relations
 ORDER BY schema_name COLLATE "C", table_name COLLATE "C";
\endif
