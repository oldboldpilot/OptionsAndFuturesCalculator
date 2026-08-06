'use client';

import React, { useState } from 'react';
import { startCheckout } from '../lib/licence';
import { useProStatus } from '../lib/useProStatus';

/**
 * What the user sees when the engine refuses a position for entitlement
 * reasons, in place of the panel that would have held the answer.
 *
 * This is the one screen where an upsell is the honest thing to show. The
 * `ProPanel` in the sidebar is deliberately understated because it sits next to
 * live quotes and interrupts nobody; this appears only after the user has
 * built a structure and been told no, which is both the moment they most want
 * the thing and the moment a dead end is most expensive. Before this existed
 * the same state rendered under the heading "Unavailable" with no way forward,
 * so the product refused a sale it had already made the case for.
 *
 * `reason` is the ENGINE's sentence, passed through untouched. The gate decides
 * what it refused and why -- multi-leg, or a licence that no longer verifies --
 * and it says so more precisely than this component could guess. Rewording it
 * here would also put a second copy of the paywall's rules in the client, where
 * it would drift from the gate the moment either changed.
 */
export const UpgradePrompt: React.FC<{ reason: string }> = ({ reason }) => {
  const { pro, setError, error } = useProStatus();
  const [busy, setBusy] = useState(false);

  const go = async (plan: 'monthly' | 'annual') => {
    setBusy(true);
    setError(null);
    try {
      window.location.href = await startCheckout(plan);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Could not start checkout.');
      setBusy(false);
    }
  };

  return (
    <div className="empty-state empty-state--gate">
      <span className="empty-state-title">Needs Pro</span>
      <span>{reason}</span>

      {/* Someone the client already believes is Pro should not be offered a
          subscription they hold. Reaching here with `pro` true means the two
          disagree -- a licence this browser still considers valid that the
          engine has stopped accepting is the realistic case -- and the useful
          thing then is the engine's sentence and a way to re-authenticate,
          not a second checkout. */}
      {pro ? (
        <span style={{ color: 'var(--color-ink-400)' }}>
          This browser holds a Pro licence the engine did not accept. Sign in again, or
          re-activate your key in the panel on the left.
        </span>
      ) : (
        <>
          <div style={{ display: 'flex', gap: '0.25rem', flexWrap: 'wrap', justifyContent: 'center' }}>
            <button onClick={() => go('monthly')} disabled={busy} style={primaryBtn}>
              {busy ? '…' : 'Start free trial'}
            </button>
            <button onClick={() => go('annual')} disabled={busy} style={secondaryBtn}>
              Annual
            </button>
          </div>
          {/* The price, and only the price. This line also carried "Already
              subscribed? Sign in on the left…", which pushed the block past the
              bottom of a panel that clips — so the one fact a paywall must
              show was the one the user could not see. ProPanel says the
              sign-in part already, on the same screen. */}
          <span style={{ color: 'var(--color-ink-400)' }}>
            7 days free, then $9.99/month or $99/year.
          </span>
        </>
      )}

      {error && <span style={{ color: 'var(--color-loss)' }}>{error}</span>}
    </div>
  );
};

// Deliberately duplicated from ProPanel rather than shared. What must not drift
// between the two is the BEHAVIOUR -- both call `startCheckout` and read
// `useProStatus`, so a change to how checkout works reaches both -- and these
// are three declarations of padding and colour. Exporting them would couple two
// components that are otherwise independent for the sake of nine lines.
const primaryBtn: React.CSSProperties = {
  padding: '0.1875rem 0.5rem',
  fontSize: 'var(--text-2xs)',
  fontWeight: 600,
  background: 'var(--color-accent)',
  color: 'var(--color-base-900, #000)',
  border: 'none',
  cursor: 'pointer',
};

const secondaryBtn: React.CSSProperties = {
  padding: '0.1875rem 0.5rem',
  fontSize: 'var(--text-2xs)',
  background: 'transparent',
  color: 'inherit',
  border: '1px solid var(--color-line)',
  cursor: 'pointer',
};

export default UpgradePrompt;
