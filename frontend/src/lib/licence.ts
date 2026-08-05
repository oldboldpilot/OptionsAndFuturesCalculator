/**
 * Pro licence handling on the client.
 *
 * The licence is a signed token, not a secret we are protecting: it is issued
 * to one subscriber and the engine verifies its signature on every call. So
 * localStorage is the right place for it -- there is nothing here an attacker
 * gains by reading, and the alternative (asking for it on every visit) would
 * make the product worse for the person who paid.
 *
 * Nothing in this file enforces anything. The gate lives in the engine, because
 * this bundle is a static export the user already has: any check here can be
 * edited out with a debugger. What this file does is make the UI *honest* about
 * what will happen -- showing which strategies need Pro before the user builds
 * one and gets refused.
 */

import { createClient } from './supabase/client';

const STORAGE_KEY = 'ofc.licence';
const LICENCE_PREFIX = 'lk_live_';

// The signed-in account's current access token, kept in memory so
// `authMetadata()` below can stay synchronous -- it is called inline at four
// gRPC call sites in useCalculatorStore that cannot await it. Supabase's own
// APIs for reading the session (`getSession`, `onAuthStateChange`) are async,
// so the token is fetched once in the background and every call after that
// reads whatever is already cached, rather than each call site awaiting a
// session lookup of its own.
let cachedAccessToken: string | null = null;
let authSubscribed = false;

/**
 * Registers the Supabase auth listener that keeps `cachedAccessToken`
 * current, the first time anything needs it. Guarded by `authSubscribed` so
 * re-importing this module (fast refresh, multiple bundle entries) does not
 * register a second listener against the same session.
 *
 * Everything here is wrapped so a missing Supabase env var, an unreachable
 * auth host, or a prerender pass all degrade to "no token" instead of
 * throwing -- this module is on the path of every gRPC call, so the free
 * tier has to keep working even when sign-in is not configured at all.
 * Gating on `window` matters for the same reason: this file is imported
 * during the static export build, where there is no browser session to read.
 */
function ensureAuthSubscription(): void {
  if (typeof window === 'undefined' || authSubscribed) return;
  authSubscribed = true;
  try {
    const supabase = createClient();
    supabase.auth.onAuthStateChange((_event, session) => {
      cachedAccessToken = session?.access_token ?? null;
    });
    // onAuthStateChange fires INITIAL_SESSION asynchronously on its own;
    // priming with getSession() as well closes the brief window on first
    // load where a call could otherwise see "signed out" when a session
    // actually exists.
    supabase.auth
      .getSession()
      .then(({ data }) => {
        cachedAccessToken = data.session?.access_token ?? null;
      })
      .catch(() => {
        cachedAccessToken = null;
      });
  } catch {
    // No Supabase configuration, or the client threw constructing itself.
    cachedAccessToken = null;
  }
}

export interface LicenceInfo {
  token: string;
  tier: string;
  expiresEpoch: number;
}

/**
 * Reads the claims out of a licence WITHOUT verifying them.
 *
 * Unverified on purpose, and safe only because of what it is used for: showing
 * the right label in the UI. The signature cannot be checked here -- doing so
 * would need the signing secret, and a secret shipped to a browser is not a
 * secret. Trusting this for access control would be the classic mistake; the
 * engine re-derives the HMAC and is the only thing that decides.
 */
export function readClaims(token: string): LicenceInfo | null {
  if (!token.startsWith(LICENCE_PREFIX)) return null;
  const body = token.slice(LICENCE_PREFIX.length);
  const dot = body.lastIndexOf('.');
  if (dot <= 0) return null;

  try {
    const b64 = body.slice(0, dot).replace(/-/g, '+').replace(/_/g, '/');
    const padded = b64 + '='.repeat((4 - (b64.length % 4)) % 4);
    const claims = JSON.parse(atob(padded)) as { t?: string; e?: number };
    if (!claims.t || typeof claims.e !== 'number') return null;
    return { token, tier: claims.t, expiresEpoch: claims.e };
  } catch {
    return null;
  }
}

export function loadLicence(): LicenceInfo | null {
  if (typeof window === 'undefined') return null;
  const raw = window.localStorage.getItem(STORAGE_KEY);
  if (!raw) return null;
  const info = readClaims(raw);
  // A licence past its expiry is dropped rather than kept and shown as
  // active, so the UI never tells someone they are subscribed while the
  // engine is refusing them.
  if (!info || info.expiresEpoch * 1000 < Date.now()) {
    window.localStorage.removeItem(STORAGE_KEY);
    return null;
  }
  return info;
}

/** Stores a licence. Returns null and stores nothing if it is not well-formed. */
export function saveLicence(token: string): LicenceInfo | null {
  const info = readClaims(token.trim());
  if (!info) return null;
  window.localStorage.setItem(STORAGE_KEY, token.trim());
  return info;
}

export function clearLicence(): void {
  if (typeof window !== 'undefined') window.localStorage.removeItem(STORAGE_KEY);
}

export function isPro(info: LicenceInfo | null): boolean {
  return info !== null && info.tier === 'pro' && info.expiresEpoch * 1000 > Date.now();
}

/**
 * gRPC-Web metadata carrying the licence and/or the signed-in session, or
 * empty when there is neither.
 *
 * Both headers are sent when both are present: the engine deliberately checks
 * `authorization` before `x-api-key` (backend/src/modules/api_key.cpp), so a
 * signed-in Pro subscriber is identified as themselves even on a page that
 * also carries the site's own publishable licence key -- dropping either
 * header here would leave that check with nothing to prefer.
 *
 * Every call site passes this rather than `{}`, so a subscriber's identity
 * travels with every request instead of only the ones someone remembered to
 * wire up. Kept synchronous because it is called inline at those call sites.
 */
export function authMetadata(): Record<string, string> {
  ensureAuthSubscription();
  const headers: Record<string, string> = {};
  const info = loadLicence();
  if (info) headers['x-api-key'] = info.token;
  if (cachedAccessToken) headers['authorization'] = `Bearer ${cachedAccessToken}`;
  return headers;
}

const BILLING_URL =
  process.env.NEXT_PUBLIC_BILLING_URL || 'https://ofc-billing.muyiwamc2.workers.dev';

/** Starts Stripe Checkout and returns the URL to send the browser to. */
export async function startCheckout(plan: 'monthly' | 'annual'): Promise<string> {
  const res = await fetch(`${BILLING_URL}/checkout`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ plan }),
  });
  if (!res.ok) throw new Error(`Checkout unavailable (${res.status})`);
  const data = (await res.json()) as { url?: string; error?: string };
  if (!data.url) throw new Error(data.error || 'Checkout returned no URL');
  return data.url;
}

/**
 * Exchanges a completed Checkout session for a licence.
 *
 * This is what makes the subscription work the moment the user returns from
 * Stripe, instead of leaving them waiting for an email before the thing they
 * just paid for does anything. The email still goes out, as the copy they keep.
 */
export async function claimLicence(sessionId: string): Promise<LicenceInfo | null> {
  const res = await fetch(`${BILLING_URL}/licence?session_id=${encodeURIComponent(sessionId)}`);
  if (!res.ok) return null;
  const data = (await res.json()) as { licence?: string };
  return data.licence ? saveLicence(data.licence) : null;
}
