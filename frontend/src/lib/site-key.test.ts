/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The rule this file holds:
 *
 *   THE SITE SENDS ITS OWN PUBLISHABLE KEY, AND A BUILD WITHOUT ONE MUST
 *   BEHAVE EXACTLY AS IT DID BEFORE THE KEY EXISTED.
 *
 * Until 2026-09-23 `authMetadata()` set `x-api-key` only from a Pro LICENCE, so
 * every anonymous visitor reached the engine with no key at all -- while both
 * this function's own comment and docs/API_SECURITY.md described the site as
 * already carrying "its own publishable key". The key existed in prose and
 * nowhere else.
 *
 * WHY THIS IS GATED, and it is not the reason you would guess. `x-api-key` is
 * NOT access control here: `FINANCE_REQUIRE_KEY` is unset, so the engine runs
 * the key gate in Observe and serves keyed and unkeyed callers alike. What the
 * key changes IMMEDIATELY is QUOTA BUCKETING -- `quota.cpp` buckets by caller
 * id, and `api_key.cpp` assigns the identity's id, tier and limits BEFORE it
 * ever consults the mode. So the moment the engine recognises this key, every
 * visitor to this site moves out of the shared `~anonymous` bucket and into one
 * named for the key.
 *
 * That is the whole reason the key is issued at tier `anonymous` rather than
 * `free`. One key shared by every visitor is ONE bucket, so a per-caller tier
 * would cap the entire site at that tier's rate -- `free` is 120 req/min
 * against anonymous's 6000, a 50x throttle on all traffic, arriving silently
 * and in Observe mode. The tier is asserted on the backend side; what this file
 * pins is the half that lives in the bundle.
 *
 * The degrade path is the check that matters most. A build with no
 * NEXT_PUBLIC_FINANCE_API_KEY must send NO header rather than an empty one: an
 * empty `x-api-key` is `Outcome::Malformed`, not `Outcome::NoKey`, and those
 * are different refusals the day the gate is ever enforced.
 */
import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';

vi.mock('./supabase/client', () => ({
  createClient: () => ({
    auth: {
      onAuthStateChange: () => ({ data: { subscription: { unsubscribe() {} } } }),
      getSession: () => Promise.resolve({ data: { session: null } }),
    },
  }),
}));

const SITE_KEY = 'pk_live_' + 'T'.repeat(43);

/** A licence token shaped the way readClaims() parses one. */
function mintLicence(tier: string, secondsFromNow: number): string {
  const claims = JSON.stringify({ t: tier, e: Math.floor(Date.now() / 1000) + secondsFromNow });
  const b64 = Buffer.from(claims, 'utf8')
    .toString('base64')
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
  return `lk_live_${b64}.signature-not-checked-on-the-client`;
}

/** Re-imports licence.ts so its build-time SITE_KEY const is re-evaluated. */
async function freshAuthMetadata() {
  vi.resetModules();
  const mod = await import('./licence');
  return mod.authMetadata;
}

afterEach(() => {
  vi.unstubAllEnvs();
  Reflect.deleteProperty(globalThis as Record<string, unknown>, 'window');
});

describe('the site publishable key', () => {
  it('is sent as x-api-key when there is no licence', async () => {
    vi.stubEnv('NEXT_PUBLIC_FINANCE_API_KEY', SITE_KEY);
    const authMetadata = await freshAuthMetadata();
    expect(authMetadata()['x-api-key']).toBe(SITE_KEY);
  });

  it('sends NO x-api-key at all when the build carries no key', async () => {
    vi.stubEnv('NEXT_PUBLIC_FINANCE_API_KEY', '');
    const authMetadata = await freshAuthMetadata();
    // Absent, not empty. An empty value is Outcome::Malformed on the engine,
    // which is a different refusal from Outcome::NoKey.
    expect('x-api-key' in authMetadata()).toBe(false);
  });

  it('never puts the site key in authorization', async () => {
    vi.stubEnv('NEXT_PUBLIC_FINANCE_API_KEY', SITE_KEY);
    const authMetadata = await freshAuthMetadata();
    expect(authMetadata()['authorization']).toBeUndefined();
  });
});

describe('a Pro licence outranks the site key', () => {
  beforeEach(() => {
    const store = new Map<string, string>();
    (globalThis as Record<string, unknown>)['window'] = {
      localStorage: {
        getItem: (k: string) => store.get(k) ?? null,
        setItem: (k: string, v: string) => void store.set(k, v),
        removeItem: (k: string) => void store.delete(k),
      },
    };
  });

  it('sends the LICENCE, not the site key, when both are available', async () => {
    const licence = mintLicence('pro', 3600);
    (globalThis as { window: { localStorage: Storage } }).window.localStorage.setItem(
      'ofc.licence',
      licence,
    );
    vi.stubEnv('NEXT_PUBLIC_FINANCE_API_KEY', SITE_KEY);
    const authMetadata = await freshAuthMetadata();
    // Only one x-api-key header exists and the licence is the entitlement; the
    // site key grants nothing a subscriber does not already have.
    expect(authMetadata()['x-api-key']).toBe(licence);
  });

  it('falls back to the site key once the licence has EXPIRED', async () => {
    (globalThis as { window: { localStorage: Storage } }).window.localStorage.setItem(
      'ofc.licence',
      mintLicence('pro', -3600),
    );
    vi.stubEnv('NEXT_PUBLIC_FINANCE_API_KEY', SITE_KEY);
    const authMetadata = await freshAuthMetadata();
    // loadLicence() drops an expired licence, so this must not send it -- and
    // must not send nothing either, which would silently lose the bucket.
    expect(authMetadata()['x-api-key']).toBe(SITE_KEY);
  });
});
