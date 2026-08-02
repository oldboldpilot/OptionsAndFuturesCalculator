'use client';

import { useEffect } from 'react';
import { create } from 'zustand';
import { claimLicence, clearLicence, isPro, loadLicence, saveLicence, type LicenceInfo } from './licence';
import { createClient } from './supabase/client';

/**
 * Reads `app_metadata.tier` out of a Supabase access token WITHOUT verifying
 * it -- the same trust boundary `readClaims` in licence.ts already uses for
 * the licence key, and safe for the same reason: verifying an HS256
 * signature needs the signing secret, and a secret shipped to a browser is
 * not a secret. This is a UI label only, telling someone they should see a
 * Pro badge before they build a strategy and get refused. It grants nothing:
 * every gRPC call also carries this same token in `authorization`
 * (see licence.ts authMetadata), and the engine independently re-derives the
 * HMAC and reads the tier server-side before honouring anything gated. If
 * this were spoofed client-side, the worst case is a badge the engine then
 * refuses -- the engine decides, this function only labels.
 */
function decodeAccountTier(accessToken: string): string | null {
  try {
    const parts = accessToken.split('.');
    if (parts.length !== 3) return null;
    const b64 = parts[1].replace(/-/g, '+').replace(/_/g, '/');
    const padded = b64 + '='.repeat((4 - (b64.length % 4)) % 4);
    const payload = JSON.parse(atob(padded)) as {
      app_metadata?: { tier?: string };
      exp?: number;
    };
    // An expired access token is not evidence of anything: Supabase refreshes
    // access tokens well before they expire, so seeing one this stale here
    // means the refresh loop stopped, not that the account's tier changed.
    // Failing closed rather than trusting a stale claim.
    if (typeof payload.exp === 'number' && payload.exp * 1000 < Date.now()) return null;
    return payload.app_metadata?.tier ?? null;
  } catch {
    return null;
  }
}

/**
 * Pro status, held in ONE place.
 *
 * A plain hook with useState would give every component its own copy: the
 * badge in the strategy list would not notice a licence activated in the panel
 * beside it, and the Stripe return effect would run once per mounting
 * component. Zustand is what the rest of this app uses for shared state, so
 * the store is the right shape here too.
 *
 * localStorage is read in an effect rather than in the initialiser because
 * this app is a static export: localStorage does not exist when the HTML is
 * generated, so reading it during render would make the server markup disagree
 * with the first client render. The first paint is therefore always "not Pro",
 * which is the safe direction -- it under-promises for a moment rather than
 * showing a subscriber's UI to someone who is not one.
 */
interface ProState {
  licence: LicenceInfo | null;
  // Tier read off the signed-in account's own Supabase session, independent
  // of any licence key. null covers "signed out", "token unparseable", and
  // "no tier claim" alike -- all three mean the same thing here: don't show
  // this account as Pro.
  accountTier: string | null;
  claiming: boolean;
  error: string | null;
  initialised: boolean;
  init: () => void;
  activate: (token: string) => boolean;
  deactivate: () => void;
  setError: (msg: string | null) => void;
}

export const useProStore = create<ProState>((set, get) => ({
  licence: null,
  accountTier: null,
  claiming: false,
  error: null,
  initialised: false,

  init: () => {
    // Guarded so mounting a second consumer does not re-run the Stripe
    // exchange or clobber a licence just activated by hand.
    if (get().initialised) return;
    set({ initialised: true, licence: loadLicence() });

    // Wrapped so a missing Supabase env var or an unreachable auth host
    // degrades to "no account tier" instead of throwing -- the licence path
    // above must keep working even when sign-in is not configured at all.
    try {
      const supabase = createClient();
      supabase.auth.onAuthStateChange((_event, session) => {
        set({ accountTier: session?.access_token ? decodeAccountTier(session.access_token) : null });
      });
      // onAuthStateChange's INITIAL_SESSION fires asynchronously on its own;
      // priming with getSession() closes the brief window on first load
      // where an already-signed-in visitor would otherwise flash "not Pro".
      supabase.auth
        .getSession()
        .then(({ data }) => {
          set({
            accountTier: data.session?.access_token ? decodeAccountTier(data.session.access_token) : null,
          });
        })
        .catch(() => set({ accountTier: null }));
    } catch {
      set({ accountTier: null });
    }

    const params = new URLSearchParams(window.location.search);
    if (params.get('checkout') !== 'success') return;
    const sessionId = params.get('session_id');
    if (!sessionId) return;

    set({ claiming: true });
    claimLicence(sessionId)
      .then((info) => {
        if (info) set({ licence: info, error: null });
        else
          set({
            error:
              'Payment went through, but the licence could not be issued yet. Check your email — it is on its way.',
          });
      })
      .catch(() =>
        set({ error: 'Could not reach the billing service. Your licence has been emailed to you.' }),
      )
      .finally(() => {
        set({ claiming: false });
        // Strip the query so a refresh does not re-run the exchange, and so the
        // session id does not sit in the URL bar waiting to be pasted into a
        // screenshot or a bug report.
        window.history.replaceState({}, '', window.location.pathname);
      });
  },

  activate: (token: string) => {
    const info = saveLicence(token);
    if (!info) {
      set({ error: 'That does not look like a licence key. It should start with lk_live_.' });
      return false;
    }
    set({ licence: info, error: null });
    return true;
  },

  deactivate: () => {
    clearLicence();
    set({ licence: null, error: null });
  },

  setError: (msg) => set({ error: msg }),
}));

/** Convenience wrapper: subscribes to the store and runs one-time init. */
export function useProStatus() {
  const s = useProStore();
  useEffect(() => {
    s.init();
    // Intentionally empty deps: init is idempotent and must run exactly once
    // per page load, not once per render or per subscribing component.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  // Pro if either path says so: a licence key, or a signed-in account whose
  // tier the billing webhook wrote onto app_metadata. Neither path can turn
  // the other one off -- someone with a valid licence but a stale/no session
  // stays Pro, and vice versa.
  return { ...s, pro: isPro(s.licence) || s.accountTier === 'pro' };
}
