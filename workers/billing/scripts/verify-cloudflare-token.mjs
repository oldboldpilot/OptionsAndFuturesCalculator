#!/usr/bin/env node
/**
 * Map what CLOUDFLARE_API_TOKEN can actually reach, without printing it.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY NOT `/user/tokens/verify`.
 *
 * That endpoint applies to USER-owned tokens only. An ACCOUNT-owned token
 * returns {"success":false,"code":1000,"message":"Invalid API Token"} there
 * while being entirely valid for zone work -- measured on this project's own
 * token, where reading `verify` as a health check produced a confident, wrong
 * "the token is dead" conclusion. So `verify` is still called here, but it is
 * reported as DIAGNOSTIC ONLY and never decides the exit code.
 *
 * WHAT DECIDES THE VERDICT. The endpoints the project actually uses. And the
 * error text discriminates, which is the part worth automating:
 *
 *   "could not find entrypoint ruleset in the <phase> phase"
 *       -> AUTHORIZED, the phase is simply empty
 *   "request is not authorized" / 403
 *       -> the permission is MISSING
 *
 * Treating both as "failed" is what hides a permission gap behind an empty
 * config, and treating both as "fine" is what hides the reverse.
 *
 * Cloudflare's edge also bans some default client signatures (error 1010)
 * BEFORE auth is consulted, which reads as a dead key when the key is fine --
 * hence an explicit User-Agent on every request.
 *
 * USAGE (the loader supplies the token; it never reaches a command line)
 *     cd workers/billing
 *     ../../scripts/run_with_env.py -- npm run cf:verify
 *
 * EXIT: 0 the token authenticates against the real API, 1 it does not,
 *       2 it is absent from the environment.
 */

import { createHash } from "node:crypto";

const API = "https://api.cloudflare.com/client/v4";
const UA = "ofc-ops-check/1.0";

/** Enough to compare two stores, not to rebuild the value. */
const witness = (v) => createHash("sha256").update(v).digest("hex").slice(0, 12);

async function call(path, token) {
  try {
    const res = await fetch(`${API}${path}`, {
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: "application/json",
        "User-Agent": UA,
      },
    });
    const text = await res.text();
    let body;
    try {
      body = JSON.parse(text);
    } catch {
      body = text.slice(0, 200);
    }
    return { status: res.status, body };
  } catch (e) {
    return { status: 0, body: `${e.name}: ${e.message}` };
  }
}

/** Cloudflare returns errors as [{code, message}]; join them for reading. */
const errText = (body) =>
  Array.isArray(body?.errors) && body.errors.length
    ? body.errors.map((e) => `${e.code}: ${e.message}`).join("; ")
    : typeof body === "string"
      ? body
      : JSON.stringify(body).slice(0, 200);

/** The distinction the memo above exists for. */
function classify(status, body) {
  if (status === 200) return "AUTHORIZED";
  const msg = errText(body).toLowerCase();
  if (msg.includes("could not find entrypoint ruleset")) return "AUTHORIZED (phase empty)";
  if (status === 403 || msg.includes("not authorized") || msg.includes("permission"))
    return "DENIED";
  return `HTTP ${status}`;
}

const token = process.env.CLOUDFLARE_API_TOKEN;
if (!token) {
  console.error(
    "CLOUDFLARE_API_TOKEN is not in the environment.\n" +
      "Run through the loader so config/.env is loaded:\n" +
      "  ../../scripts/run_with_env.py -- npm run cf:verify",
  );
  process.exit(2);
}

console.log(`token: ${token.length} chars  sha256:${witness(token)}`);
console.log();

// Diagnostic only -- see the header. This line must never decide anything.
const v = await call("/user/tokens/verify", token);
console.log(
  `/user/tokens/verify -> HTTP ${v.status}  ${v.status === 200 ? v.body?.result?.status : errText(v.body)}`,
);
console.log("  (account-owned tokens fail here while working fine; diagnostic only)");
console.log();

// The verdict: can it read zones, which is what every real operation starts with?
const zones = await call("/zones?per_page=50", token);
console.log(`/zones -> HTTP ${zones.status}`);
if (zones.status !== 200) {
  console.log(`  ${errText(zones.body)}`);
  console.log("\nVERDICT: the token does NOT authenticate against the zone API.");
  process.exit(1);
}

const rows = zones.body?.result ?? [];
console.log(`  ${rows.length} zone(s) visible to this token:`);
for (const z of rows) {
  console.log(`    ${String(z.name).padEnd(34)} status=${z.status}  plan=${z.plan?.name ?? "?"}`);
}
console.log();

// Phase reach, for the zones this project serves.
const PHASES = [
  "http_request_transform",
  "http_request_firewall_custom",
  "http_request_dynamic_redirect",
];
const interesting = rows.filter((z) =>
  /optionsandfuturescalculator|mortgagefvcalculator/.test(z.name),
);

for (const z of interesting) {
  console.log(`${z.name}:`);
  const rs = await call(`/zones/${z.id}/rulesets`, token);
  console.log(`  rulesets list                     ${classify(rs.status, rs.body)}`);
  for (const phase of PHASES) {
    const r = await call(`/zones/${z.id}/rulesets/phases/${phase}/entrypoint`, token);
    console.log(`  ${phase.padEnd(34)}${classify(r.status, r.body)}`);
  }
  console.log();
}

console.log("VERDICT: the token authenticates and can read zones.");
process.exit(0);
