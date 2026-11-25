-- Fingerprint of the mortgage database, one line per fact, for scripts/verify_restore_mortgage.sh.
--
-- @author Olumuyiwa Oluwasanmi
--
-- Run with  psql -X -At -F'|' -v ON_ERROR_STOP=1 -f this.sql  against BOTH the live database (read-only; the
-- backup script wraps it in a read-only transaction) and the restored copy. The two outputs are compared.
-- Nothing here selects a row's contents: only names, counts and role membership. No credential, hash or
-- personal datum can appear in it.
--
-- Line kinds:
--   schema|<name>                       every non-system schema
--   table|<schema>.<name>               every ordinary or partitioned table outside system schemas
--   member|<role>|<member of>           role membership among the non-system roles
--   grants|<n>                          table privileges granted to a non-system role, over auth and public
--   policies|<n>                        row-level security policies, over auth and public
--   rls|<n>                             tables with row-level security enabled, over auth and public
--   rows|<schema>.<name>|<count>        exact row count per table (the only DATA line)
set default_transaction_read_only = on;

select 'schema|' || nspname from pg_namespace
 where nspname !~ '^pg_' and nspname <> 'information_schema' order by 1;

select 'table|' || n.nspname || '.' || c.relname
  from pg_class c join pg_namespace n on n.oid = c.relnamespace
 where c.relkind in ('r', 'p') and n.nspname !~ '^pg_' and n.nspname <> 'information_schema' order by 1;

select 'member|' || r.rolname || '|' || g.rolname
  from pg_auth_members m
  join pg_roles r on r.oid = m.member and r.rolname !~ '^pg_'
  join pg_roles g on g.oid = m.roleid and g.rolname !~ '^pg_' order by 1;

select 'grants|' || count(*)
  from pg_class c join pg_namespace n on n.oid = c.relnamespace,
       lateral aclexplode(c.relacl) a
  join pg_roles r on r.oid = a.grantee
 where n.nspname in ('auth', 'public') and c.relkind in ('r', 'p', 'v', 'S')
   -- the owner's own entry and postgres are excluded: restore uses --no-owner, so ownership (and the
   -- implicit owner ACL entry that follows it) legitimately differs between source and copy
   and a.grantee <> c.relowner and r.rolname <> 'postgres';

select 'policies|' || count(*)
  from pg_policy p join pg_class c on c.oid = p.polrelid join pg_namespace n on n.oid = c.relnamespace
 where n.nspname in ('auth', 'public');

select 'rls|' || count(*)
  from pg_class c join pg_namespace n on n.oid = c.relnamespace
 where n.nspname in ('auth', 'public') and c.relkind in ('r', 'p') and c.relrowsecurity;

select 'rows|' || n.nspname || '.' || c.relname || '|' ||
       (xpath('/row/c/text()', query_to_xml(format('select count(*) as c from %I.%I', n.nspname, c.relname), false, true, '')))[1]::text
  from pg_class c join pg_namespace n on n.oid = c.relnamespace
 where c.relkind in ('r', 'p') and n.nspname !~ '^pg_' and n.nspname <> 'information_schema' order by 1;
