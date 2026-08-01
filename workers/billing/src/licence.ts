/**
 * Licence minting.
 *
 * This file is the twin of `verify_licence` in
 * backend/src/modules/api_key.cpp. The two must agree byte for byte: the
 * engine recomputes this exact HMAC over this exact payload encoding, and any
 * divergence -- a padded base64, a reordered JSON key, a different truncation
 * -- makes every licence this Worker issues unverifiable. Change one side and
 * you must change the other.
 *
 *   token   = "lk_live_" + payload_b64url + "." + signature_b64url
 *   payload = {"s":<stripe customer>,"t":<tier>,"e":<expiry epoch>,"v":1}
 *   sig     = base64url( HMAC-SHA512(secret, payload_b64url)[0..32] )
 */

const LICENCE_PREFIX = 'lk_live_';

/**
 * Unpadded base64url.
 *
 * Unpadded because the engine's decoder rejects any character outside the
 * base64url alphabet, and '=' is outside it. Padding here would make every
 * licence fail verification.
 */
function b64url(bytes: Uint8Array): string {
  let binary = '';
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

/**
 * Grace added to the subscription's period end.
 *
 * Renewal webhooks are not instantaneous, and a subscriber whose licence
 * expired at the exact second their renewal was still processing would be
 * locked out of something they had just paid for. Three days costs nothing
 * against a monthly cycle and removes that class of support ticket entirely.
 */
const GRACE_DAYS = 3;

export interface LicenceClaims {
  customerId: string;
  tier: 'pro' | 'free';
  periodEndEpoch: number;
}

export async function mintLicence(secret: string, claims: LicenceClaims): Promise<string> {
  if (!secret) throw new Error('LICENCE_SIGNING_KEY is not configured');

  // Key order is fixed and the separators are compact, because this exact
  // string is what gets signed. JSON.stringify over an object literal
  // preserves insertion order for string keys, which is what makes this
  // reproducible.
  const payload = JSON.stringify({
    s: claims.customerId,
    t: claims.tier,
    e: claims.periodEndEpoch + GRACE_DAYS * 86400,
    v: 1,
  });

  const payloadB64 = b64url(new TextEncoder().encode(payload));

  const key = await crypto.subtle.importKey(
    'raw',
    new TextEncoder().encode(secret),
    { name: 'HMAC', hash: 'SHA-512' },
    false,
    ['sign'],
  );
  const mac = new Uint8Array(
    await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(payloadB64)),
  );

  // Truncated to 256 bits to match the engine. HMAC's strength is bounded by
  // the narrower of key and output, so this is not a weakening -- it halves a
  // token a person has to paste.
  return `${LICENCE_PREFIX}${payloadB64}.${b64url(mac.slice(0, 32))}`;
}

/**
 * Verifies a Stripe webhook signature.
 *
 * Written out rather than pulled from the Stripe SDK because the SDK's own
 * verifier needs Node crypto, which Workers do not have. The check is the one
 * Stripe documents: HMAC-SHA256 over `timestamp.body`, compared against the
 * `v1` scheme in the Stripe-Signature header.
 *
 * Without this, the endpoint would mint Pro licences for anyone who could POST
 * to it -- the webhook URL is public, so the signature IS the authentication.
 */
export async function verifyStripeSignature(
  secret: string,
  header: string | null,
  body: string,
  toleranceSeconds = 300,
): Promise<boolean> {
  if (!secret || !header) return false;

  let timestamp = '';
  const signatures: string[] = [];
  for (const part of header.split(',')) {
    const [k, v] = part.split('=', 2);
    if (k?.trim() === 't') timestamp = v ?? '';
    if (k?.trim() === 'v1' && v) signatures.push(v);
  }
  if (!timestamp || signatures.length === 0) return false;

  // Replay window. Without it a captured request stays valid forever, so an
  // attacker who saw one legitimate webhook could resend it indefinitely.
  const age = Math.abs(Math.floor(Date.now() / 1000) - Number(timestamp));
  if (!Number.isFinite(age) || age > toleranceSeconds) return false;

  const key = await crypto.subtle.importKey(
    'raw',
    new TextEncoder().encode(secret),
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign'],
  );
  const mac = new Uint8Array(
    await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(`${timestamp}.${body}`)),
  );
  const expected = [...mac].map((b) => b.toString(16).padStart(2, '0')).join('');

  return signatures.some((s) => timingSafeEqual(s, expected));
}

/** Constant-time string comparison; `===` leaks how many characters matched. */
function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}
