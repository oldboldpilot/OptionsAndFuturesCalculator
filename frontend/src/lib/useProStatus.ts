'use client';

import { useEffect } from 'react';
import { create } from 'zustand';
import { claimLicence, clearLicence, isPro, loadLicence, saveLicence, type LicenceInfo } from './licence';

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
  claiming: false,
  error: null,
  initialised: false,

  init: () => {
    // Guarded so mounting a second consumer does not re-run the Stripe
    // exchange or clobber a licence just activated by hand.
    if (get().initialised) return;
    set({ initialised: true, licence: loadLicence() });

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
  return { ...s, pro: isPro(s.licence) };
}
