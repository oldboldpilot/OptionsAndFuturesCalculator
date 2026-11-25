-- A GoTrue-shaped auth.users for SCRATCH databases only.
--
-- @author Olumuyiwa Oluwasanmi
--
-- Production's auth schema belongs to GoTrue, which migrates it itself, so no
-- migration here may create it. A scratch database used by the drift gate and
-- its self-test has no GoTrue, yet migration 0001 holds a foreign key to
-- auth.users(id). This is the minimum that makes that real: the id key, and
-- the eight token columns migration 09 repairs, shaped exactly as GoTrue
-- v2.151 ships them (four DEFAULT '', four without) so the snapshot's
-- auth_column lines are produced from the same starting point as production.
--
-- NEVER run this against a hosted database.
CREATE SCHEMA IF NOT EXISTS auth;
CREATE TABLE IF NOT EXISTS auth.users (
    id                         uuid PRIMARY KEY,
    email                      text,
    encrypted_password         varchar(255),
    confirmation_token         varchar(255),
    recovery_token             varchar(255),
    email_change               varchar(255),
    email_change_token_new     varchar(255),
    email_change_token_current varchar(255) DEFAULT '',
    phone_change               text         DEFAULT '',
    phone_change_token         varchar(255) DEFAULT '',
    reauthentication_token     varchar(255) DEFAULT ''
);
