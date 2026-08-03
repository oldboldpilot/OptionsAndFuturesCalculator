// Mints the full pro-gate credential matrix -- valid and invalid -- into a
// directory, so every probe transport asserts against the SAME credentials.
//
//   node scripts/mint_pro_gate_creds.mjs <out_dir> <path/to/licence.ts>
//
// Licences go through the Worker's real mintLicence, never a hand-rolled
// payload: the signed body is {s,t,e,v}, and a probe that invents a different
// shape tests nothing but its own guess.
//
// Requires LICENCE_SIGNING_KEY and SUPABASE_JWT_SECRET in the environment,
// both matching the engine under test.
//
// @author Olumuyiwa Oluwasanmi
import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const licenceTs = process.argv[3];
if (!dir || !licenceTs) {
  console.error('usage: mint_pro_gate_creds.mjs <out_dir> <licence.ts>');
  process.exit(2);
}

const { mintLicence } = await import(pathToFileURL(path.resolve(licenceTs)).href);

const lkey = process.env.LICENCE_SIGNING_KEY;
const jkey = process.env.SUPABASE_JWT_SECRET;
if (!lkey || !jkey) {
  console.error('LICENCE_SIGNING_KEY and SUPABASE_JWT_SECRET must both be set');
  process.exit(2);
}

const now = Math.floor(Date.now() / 1000);
const b64 = (s) =>
  Buffer.from(s).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const w = (n, v) => fs.writeFileSync(`${dir}/${n}`, v);

// --- Licences, all through the Worker's real mintLicence. ---
w('lic.valid', await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now + 30 * 86400 }));

// Well past mintLicence's GRACE_DAYS, or this row would still be live and
// would prove nothing about expiry.
w('lic.expired', await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now - 10 * 86400 }));

// Sign a FREE licence, then swap the payload for a Pro one and keep the
// original signature. This is the exact attack the signature exists to stop.
const freeTok = await mintLicence(lkey, {
  customerId: 'cus_probe', tier: 'free', periodEndEpoch: now + 30 * 86400 });
const sig = freeTok.slice(freeTok.lastIndexOf('.') + 1);
w('lic.tampered',
  `lk_live_${b64(JSON.stringify({ s: 'cus_probe', t: 'pro', e: now + 33 * 86400, v: 1 }))}.${sig}`);

// Someone who worked out the FORMAT but not the secret.
w('lic.wrongkey', await mintLicence(crypto.randomBytes(48).toString('base64'), {
  customerId: 'cus_probe', tier: 'pro', periodEndEpoch: now + 30 * 86400 }));

// --- Supabase-shaped HS256 access tokens, as GoTrue issues them. ---
const jwt = (claims, { alg = 'HS256', swap = null } = {}) => {
  const h = b64(JSON.stringify({ alg, typ: 'JWT' }));
  let p = b64(JSON.stringify(claims));
  if (alg === 'none') return `${h}.${p}.`;
  const s = b64(crypto.createHmac('sha256', jkey).update(`${h}.${p}`).digest());
  if (swap) p = b64(JSON.stringify({ ...claims, ...swap }));
  return `${h}.${p}.${s}`;
};
const base = (extra) => ({
  sub: '11111111-2222-3333-4444-555555555555', aud: 'authenticated',
  role: 'authenticated', email: 'probe@example.com',
  iat: now, exp: now + 3600, app_metadata: { provider: 'email' }, user_metadata: {}, ...extra,
});

w('jwt.pro',  jwt(base({ app_metadata: { provider: 'email', tier: 'pro' } })));
w('jwt.free', jwt(base({ app_metadata: { provider: 'email', tier: 'free' } })));

// user_metadata IS writable from the browser with the anon key, so a verifier
// that read the tier from there would be handing out free upgrades.
w('jwt.usermeta', jwt(base({
  app_metadata: { provider: 'email', tier: 'free' }, user_metadata: { tier: 'pro' } })));

// Signed as free, payload swapped to pro afterwards.
w('jwt.tampered', jwt(base({ app_metadata: { provider: 'email', tier: 'free' } }),
  { swap: { app_metadata: { provider: 'email', tier: 'pro' } } }));

w('jwt.expired', jwt(base({ exp: now - 60, app_metadata: { provider: 'email', tier: 'pro' } })));

// alg:none asks to be accepted with no signature at all -- the classic JWT break.
w('jwt.algnone', jwt(base({ app_metadata: { provider: 'email', tier: 'pro' } }), { alg: 'none' }));

w('jwt.garbage', 'not-a-jwt-at-all');
w('lic.garbage', 'lk_live_bm90cmVhbA.bm90cmVhbA');
