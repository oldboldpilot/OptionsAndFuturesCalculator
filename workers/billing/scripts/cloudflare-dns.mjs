#!/usr/bin/env node
/**
 * List (and optionally add) DNS records for a zone, without printing the token.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY A SEPARATE TOOL. verify-cloudflare-token.mjs answers "what can this token
 * reach"; this answers "what does the zone actually say". The split matters
 * because the two need different permissions: zone read is granted to this
 * project's token, DNS record read is a SEPARATE permission group, and DNS
 * record EDIT is a third. A tool that conflates them reports "no records" when
 * the truth is "not authorized to look", which is the same misreading that
 * `/user/tokens/verify` produces for an account-owned token.
 *
 * So every failure here is classified rather than collapsed:
 *
 *   403 / "not authorized"  -> the permission is missing from the token
 *   200 with an empty list  -> authorized, and the zone really has none
 *
 * Cloudflare's edge also bans some default client signatures with error 1010
 * BEFORE auth is consulted, so every request names a client explicitly.
 *
 * USAGE (the loader supplies the token; it never reaches a command line)
 *     cd workers/billing
 *     ../../scripts/run_with_env.py -- npm run cf:dns
 *     ../../scripts/run_with_env.py -- npm run cf:dns:txt
 *
 * Reads DNS_ZONE and DNS_TYPE from the environment, defaulting to
 * mortgagefvcalculator.com and all types.
 *
 * EXIT: 0 listed, 1 denied or failed, 2 the token is absent.
 */

import { createHash } from "node:crypto";

const API = "https://api.cloudflare.com/client/v4";
const UA = "ofc-ops-check/1.0";

const witness = (v) => createHash("sha256").update(v).digest("hex").slice(0, 12);

async function cf(path, token) {
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

const errText = (body) =>
  Array.isArray(body?.errors) && body.errors.length
    ? body.errors.map((e) => `${e.code}: ${e.message}`).join("; ")
    : typeof body === "string"
      ? body
      : JSON.stringify(body).slice(0, 200);

/** The distinction this tool exists for. */
function classify(status, body) {
  if (status === 200) return "AUTHORIZED";
  const msg = errText(body).toLowerCase();
  if (status === 403 || msg.includes("not authorized") || msg.includes("permission"))
    return "DENIED (permission missing from the token)";
  if (status === 404) return "NOT FOUND";
  return `HTTP ${status}`;
}

// WHICH TOKEN. This account holds several, and they differ in permission, not
// just in name: CLOUDFLARE_API_TOKEN carries Zone:Read and is DENIED on
// dns_records (403, code 10000 "Authentication error"), while
// CLOUDFLARE_EDIT_ZONES_TOKEN is the one provisioned for zone edits. Probing
// the wrong one reports "the permission is missing" about a token nobody
// intended to use for this, so the variable is a parameter and the name used is
// printed beside its fingerprint.
const tokenVar = process.env.CF_TOKEN_VAR || "CLOUDFLARE_EDIT_ZONES_TOKEN";
const token = process.env[tokenVar];
if (!token) {
  console.error(
    `${tokenVar} is not in the environment.\n` +
      "Run through the loader: ../../scripts/run_with_env.py -- npm run cf:dns",
  );
  process.exit(2);
}

const zoneName = process.env.DNS_ZONE || "mortgagefvcalculator.com";
const wantType = process.env.DNS_TYPE || "";

console.log(`token: ${tokenVar}  ${token.length} chars  sha256:${witness(token)}`);
console.log(`zone:  ${zoneName}${wantType ? `  type=${wantType}` : ""}`);
console.log();

const zres = await cf(`/zones?name=${encodeURIComponent(zoneName)}`, token);
if (zres.status !== 200) {
  console.log(`zone lookup -> ${classify(zres.status, zres.body)}`);
  console.log(`  ${errText(zres.body)}`);
  process.exit(1);
}
const zone = (zres.body?.result ?? [])[0];
if (!zone) {
  console.log(`zone lookup -> HTTP 200 but no zone named ${zoneName} is visible to this token`);
  process.exit(1);
}
console.log(`zone id resolved (status=${zone.status})`);

const q = wantType ? `?type=${encodeURIComponent(wantType)}&per_page=100` : "?per_page=100";
const rres = await cf(`/zones/${zone.id}/dns_records${q}`, token);
console.log(`dns_records read -> ${classify(rres.status, rres.body)}`);
if (rres.status !== 200) {
  console.log(`  ${errText(rres.body)}`);
  console.log();
  console.log("A DNS record read is its own permission group (Zone:DNS:Read).");
  console.log("Zone:Read alone -- which this token has -- does not include it.");
  process.exit(1);
}

const rows = rres.body?.result ?? [];
console.log(`  ${rows.length} record(s)`);
for (const r of rows) {
  // A TXT value can be a verification token; it is public by design (it is
  // published in DNS) so printing it is how the tool is useful at all.
  const val = String(r.content ?? "").slice(0, 120);
  console.log(`  ${String(r.type).padEnd(6)} ${String(r.name).padEnd(44)} ${val}`);
}
if (!rows.length) {
  console.log("  (authorized, and the zone genuinely has no record of this type)");
}

// ---------------------------------------------------------------------------
// OPTIONAL WRITE: add a TXT record at the apex.
//
// Driven by DNS_TXT_NAME + DNS_TXT_VALUE so the value arrives through the
// loader's environment rather than a command line. Writing is IDEMPOTENT by
// content: if a record with the same name and value already exists the call is
// skipped, so re-running the verification chain never stacks duplicates (and a
// duplicate google-site-verification TXT is not harmless -- it makes the zone's
// answer ambiguous).
// ---------------------------------------------------------------------------
const txtName = process.env.DNS_TXT_NAME;
const txtValue = process.env.DNS_TXT_VALUE;
if (txtValue) {
  const name = txtName || zoneName;
  const existing = rows.find(
    (r) => String(r.name) === name && String(r.content).replace(/^"|"$/g, "") === txtValue,
  );
  if (existing) {
    console.log();
    console.log(`TXT ${name} already carries that exact value -- nothing to do.`);
    process.exit(0);
  }
  console.log();
  console.log(`adding TXT ${name} ...`);
  const res = await fetch(`${API}/zones/${zone.id}/dns_records`, {
    method: "POST",
    headers: {
      Authorization: `Bearer ${token}`,
      "Content-Type": "application/json",
      "User-Agent": UA,
    },
    body: JSON.stringify({ type: "TXT", name, content: txtValue, ttl: 300 }),
  });
  const text = await res.text();
  let body;
  try {
    body = JSON.parse(text);
  } catch {
    body = text.slice(0, 200);
  }
  if (res.status !== 200 && res.status !== 201) {
    console.log(`  write -> ${classify(res.status, body)}`);
    console.log(`  ${errText(body)}`);
    process.exit(1);
  }
  console.log(`  written, id ${body?.result?.id ?? "?"} ttl ${body?.result?.ttl ?? "?"}`);
}
process.exit(0);
