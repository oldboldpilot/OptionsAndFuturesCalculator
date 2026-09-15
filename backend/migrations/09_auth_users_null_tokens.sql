-- GoTrue scans four auth.users columns into a non-nullable Go string, and a
-- restored row can hold NULL there.
--
-- @author Olumuyiwa Oluwasanmi
--
-- SYMPTOM: every admin endpoint answers 500.
--
--   GET /auth/v1/admin/users
--   {"code":500,"error_code":"unexpected_failure","msg":"Database error finding users"}
--   [ERRO] unable to fetch records: sql: Scan error on column index 3,
--          name "confirmation_token": converting NULL to string is unsupported
--
-- The value is NOT a guess, and the schema says so itself. auth.users has eight
-- such columns; FOUR carry `DEFAULT ''` and four do not, and the split is exact:
--
--   DEFAULT ''  -> email_change_token_current, phone_change,
--                  phone_change_token, reauthentication_token   -- 0 NULLs
--   no default  -> confirmation_token, recovery_token,
--                  email_change, email_change_token_new         -- ALL NULL
--
-- Supabase hit this and added the default to the columns it added later; the
-- original four were never backfilled. So '' is what the schema intends.
--
-- NOT CAUSED BY THE 2026-09-15 DATABASE MOVE, and that was checked rather than
-- assumed: the pre-drop dump of the OLD database carries `\N` in all four for
-- the same row. pg_dump/pg_restore preserved them exactly. The defect is
-- inherited from the Lovable-era data and was live before the move.
--
-- Both halves are here on purpose. The UPDATE fixes the rows that exist; the
-- DEFAULT stops the next import reintroducing it, which is the only path that
-- produces it -- GoTrue always writes these explicitly. Fixing only the rows
-- would be repairing the instance and leaving the class.

DO $$
DECLARE
    col   text;
    fixed int;
    total int := 0;
BEGIN
    IF to_regclass('auth.users') IS NULL THEN
        RAISE NOTICE 'auth.users absent (no GoTrue here); nothing to do';
        RETURN;
    END IF;

    FOREACH col IN ARRAY ARRAY['confirmation_token', 'recovery_token',
                               'email_change', 'email_change_token_new',
                               'email_change_token_current', 'phone_change',
                               'phone_change_token', 'reauthentication_token']
    LOOP
        -- Skip a column this GoTrue version does not have, rather than failing
        -- the migration: the set has grown over releases.
        IF NOT EXISTS (SELECT 1 FROM information_schema.columns
                        WHERE table_schema = 'auth' AND table_name = 'users'
                          AND column_name = col) THEN
            CONTINUE;
        END IF;

        EXECUTE format('UPDATE auth.users SET %I = %L WHERE %I IS NULL', col, '', col);
        GET DIAGNOSTICS fixed = ROW_COUNT;
        total := total + fixed;
        IF fixed > 0 THEN
            RAISE NOTICE 'auth.users.%: % NULL -> ''''', col, fixed;
        END IF;

        EXECUTE format('ALTER TABLE auth.users ALTER COLUMN %I SET DEFAULT %L', col, '');
    END LOOP;

    RAISE NOTICE 'auth.users token columns repaired: % row-column(s)', total;
END;
$$;

-- ---------------------------------------------------------------------------
-- encrypted_password, handled separately because it is not just another token.
--
-- Supabase's hosted Postgres permits NULL here -- that is how it represents an
-- account with no password (magic-link / OAuth only) -- and gotrue v2.151.0
-- scans it into a non-nullable Go string, so the admin listing still 500s once
-- the token columns above are clean. Measured: with all eight token columns at
-- zero NULLs, GET /auth/v1/admin/users was still 500.
--
-- The EMPTY STRING is gotrue's OWN spelling of "no password", not a
-- placeholder invented here: its HasPassword() tests the column against empty.
-- This preserves the account's meaning rather than changing it.
--
-- IT IS NOT A SIGN-IN BYPASS, and that is ASSERTED rather than argued: empty is
-- not a well-formed bcrypt digest, so the comparison fails for EVERY candidate
-- including the empty one. Both directions are checked against the live
-- endpoint after this migration -- a wrong secret and an empty secret must each
-- come back 400 invalid_grant, never 200.
--
-- Only NULL rows are touched, so a real digest can never be overwritten.
--
-- phone is deliberately LEFT NULL. gotrue models it as a nullable string and
-- auth.users carries a UNIQUE index on it, so writing empty would both invent a
-- phone number the account does not have and collide the moment a second such
-- account exists.
-- ---------------------------------------------------------------------------
DO $$
DECLARE fixed int;
BEGIN
    IF to_regclass('auth.users') IS NULL THEN RETURN; END IF;

    UPDATE auth.users SET encrypted_password = '' WHERE encrypted_password IS NULL;
    GET DIAGNOSTICS fixed = ROW_COUNT;
    IF fixed > 0 THEN
        RAISE NOTICE 'auth.users.encrypted_password: % NULL -> empty (no-secret account)', fixed;
    END IF;
END;
$$;
