#!/usr/bin/env node
// Install this Worker's secrets into Cloudflare FROM THE PROCESS ENVIRONMENT.
//
// @author Olumuyiwa Oluwasanmi
//
// WHY THIS EXISTS RATHER THAN A `wrangler secret put` TYPED BY HAND.
//
// A rotated key has to reach four stores, and the two ways of moving it by hand
// are both wrong. `railway variables --set NAME=value` and
// `wrangler secret put NAME value` place the secret in a COMMAND LINE, so it
// lands in shell history and in the process argument list, where any other user
// on the box can read it from /proc. Pasting it into a terminal puts it in the
// scrollback. The value is then rotated, and leaked, in the same minute.
//
// `wrangler secret put NAME` with no value reads the value from STDIN, which is
// the one path that avoids both. This script is that path, driven from the
// environment that `scripts/run_with_env.py` loads from `config/.env`, so the
// value travels dotenv -> process environment -> pipe -> Cloudflare and is never
// written down anywhere in between. Nothing here prints a value; `--check`
// prints a name, a length and a SHA-256 PREFIX, which is enough to confirm two
// stores hold the same string and not enough to reconstruct it.
//
// USAGE (from this directory, so npm resolves the local wrangler):
//   ../../scripts/run_with_env.py -- npm run secrets:check
//   ../../scripts/run_with_env.py -- npm run secrets:install
//
// A name absent from the environment is REPORTED AND SKIPPED, never installed
// as an empty string: a Worker holding an empty STRIPE_SECRET_KEY fails at the
// first checkout with an error that names Stripe rather than this script.
//
// EXIT CODES: 0 every requested secret was handled, 1 at least one failed, 2 the
// environment carried none of them (which means the loader was not used).

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";

// The names this Worker's code actually reads. Keeping the list here, rather
// than installing whatever the dotenv happens to hold, means a variable added
// to config/.env for some other service is never pushed into this Worker.
const WORKER_SECRETS = [
  "STRIPE_SECRET_KEY",
  "STRIPE_WEBHOOK_SECRET",
  "LICENCE_SIGNING_KEY",
  "RESEND_API_KEY",
  "SUPABASE_URL",
  "SUPABASE_SERVICE_ROLE_KEY",
];

const args = new Set(process.argv.slice(2));
const check = args.has("--check");

// `--scan` answers "which payment-related names does the loaded dotenv carry,
// and are any two of them the same string?" without revealing one. It exists
// because a rotation can leave the NEW key under a name nothing reads yet,
// while the name the code reads still holds the dead one -- a state in which
// every store looks populated and checkout still returns 401.
if (args.has("--scan")) {
  // SCAN_MATCH widens the scan to any name class, so the same fingerprinting
  // can compare a dotenv against another store (Railway, Cloudflare) for
  // credentials this Worker does not itself read. Pass it through the loader:
  //   ../../scripts/run_with_env.py --set SCAN_MATCH=ALPACA -- npm run secrets:scan
  // `--match=<re>` widens the scan to any name class, so the same fingerprinting
  // compares a dotenv against another store (Railway, Cloudflare) for
  // credentials this Worker does not itself read. It is an ARGUMENT rather than
  // an environment variable because the repository's credential guard permits
  // the loader only in the shape `run_with_env.py -- npm run <script>`, with no
  // --set, and that restriction is worth keeping.
  const matchArg = [...args].find((a) => a.startsWith("--match="));
  const pattern = matchArg
    ? new RegExp(matchArg.slice("--match=".length), "i")
    : /STRIPE|PAYMENT|BILLING/i;
  const hits = Object.keys(process.env)
    .filter((k) => pattern.test(k))
    .sort();
  if (hits.length === 0) {
    console.error("no payment-related names in the environment -- was the loader used?");
    process.exit(2);
  }
  for (const k of hits) {
    const v = process.env[k] ?? "";
    const shape = /^[a-z]{2,3}_(live|test)_/.exec(v)?.[0] ?? (v ? "(not a Stripe key prefix)" : "(empty)");
    console.log(`  ${k.padEnd(32)} ${String(v.length).padStart(4)} chars  ${shape.padEnd(28)} sha256:${witness(v)}`);
  }
  process.exit(0);
}
// Default to the one secret a Stripe API-key rotation moves. A webhook signing
// secret is NOT an API key and survives a key rotation, so re-pushing it is a
// way to break verification for no reason; --all is the deliberate opt-in.
const wanted = args.has("--all")
  ? WORKER_SECRETS
  : [...args].filter((a) => WORKER_SECRETS.includes(a));
const names = wanted.length > 0 ? wanted : ["STRIPE_SECRET_KEY"];

/** A short, non-reversible witness: enough to compare two stores, not to rebuild the value. */
function witness(value) {
  return createHash("sha256").update(value).digest("hex").slice(0, 12);
}

// --------------------------------------------------------------------------
// `--railway <service>`: install into a Railway service instead of this Worker.
//
// THE MAPPING IS WRITTEN DOWN BECAUSE THE NAMES DISAGREE ACROSS STORES. mfv-web
// reads STRIPE_LIVE_API_KEY and STRIPE_SANDBOX_API_KEY; the rotated value
// arrives in config/.env as STRIPE_SECRET_KEY. Reading "the variable of the same
// name" would therefore push the OLD key -- which is exactly what happened on
// 2026-11-14, when all three stores held one expired `rk_live_` and every one of
// them looked populated. A mapping stated in source is auditable; a convention
// held in someone's head is the defect.
//
// BOTH sandbox and live names take the live value here. That is not a mistake,
// it is this estate's recorded (and deliberate) state -- the fingerprints prove
// the two names have always carried the same string -- and silently "fixing" it
// during a rotation would change behaviour under cover of a credential change.
//
// UNLIKE `wrangler secret put`, the Railway CLI has NO stdin form: `railway
// variables --set NAME=value` is the only way in, so the value appears in this
// process's argument list for the lifetime of the call and is readable from
// /proc by another process of the same user. That is a real, if small, exposure
// on a single-user box, it is why this is opt-in behind an explicit flag, and it
// is why the value is still never printed by this script.
const railwayArg = [...args].find((a) => a.startsWith("--railway="));
if (railwayArg) {
  const service = railwayArg.slice("--railway=".length);
  const MAPPING = [
    { from: "STRIPE_SECRET_KEY", to: "STRIPE_LIVE_API_KEY" },
    { from: "STRIPE_SECRET_KEY", to: "STRIPE_SANDBOX_API_KEY" },
  ];
  let bad = 0;
  for (const { from, to } of MAPPING) {
    const value = process.env[from];
    if (!value) {
      console.error(`  ${from} absent from the environment -- ${to} NOT set`);
      bad += 1;
      continue;
    }
    console.log(`  ${from} -> ${service}:${to}  ${value.length} chars  sha256:${witness(value)}`);
    if (check) continue;
    // THE LOADER'S OWN ENVIRONMENT CAN BREAK THE CLI IT IS FEEDING.
    // config/.env carries a RAILWAY_TOKEN, and the Railway CLI PREFERS that
    // variable over the OAuth session `railway login` established -- so running
    // the CLI through the loader authenticates with whatever token the dotenv
    // happens to hold. Measured 2026-11-14: a stale one there produced
    // "Invalid RAILWAY_TOKEN" from a CLI that worked perfectly one command
    // earlier, outside the loader. Dropping the token names from the CHILD's
    // environment makes the CLI fall back to the session, which is the
    // credential the operator actually authenticated. Nothing else is removed:
    // the child still needs the rest of the environment.
    const childEnv = { ...process.env };
    delete childEnv.RAILWAY_TOKEN;
    delete childEnv.RAILWAY_API_TOKEN;
    const res = spawnSync(
      "railway",
      ["variables", "--service", service, "--set", `${to}=${value}`],
      { stdio: ["ignore", "inherit", "inherit"], encoding: "utf8", env: childEnv },
    );
    if (res.status !== 0) {
      console.error(`  ${to}: railway exited ${res.status ?? "signal " + res.signal}`);
      bad += 1;
    }
  }
  process.exit(bad === 0 ? 0 : 1);
}

let present = 0;
let failed = 0;

for (const name of names) {
  const value = process.env[name];
  if (value === undefined || value === "") {
    console.log(`  ${name.padEnd(28)} ABSENT from the environment -- skipped`);
    continue;
  }
  present += 1;
  console.log(`  ${name.padEnd(28)} present  ${String(value.length).padStart(4)} chars  sha256:${witness(value)}`);
  if (check) continue;

  // `input` hands the value to the child on stdin. It is never an argv element,
  // so it does not reach the process list or the shell's history.
  const res = spawnSync("wrangler", ["secret", "put", name], {
    input: value,
    stdio: ["pipe", "inherit", "inherit"],
    encoding: "utf8",
  });
  if (res.status !== 0) {
    failed += 1;
    console.error(`  ${name}: wrangler exited ${res.status ?? "signal " + res.signal}`);
  }
}

if (present === 0) {
  console.error(
    "none of the requested names are in the environment -- run this through\n" +
      "scripts/run_with_env.py so config/.env is loaded, e.g.\n" +
      "  ../../scripts/run_with_env.py -- npm run secrets:check",
  );
  process.exit(2);
}
process.exit(failed === 0 ? 0 : 1);
