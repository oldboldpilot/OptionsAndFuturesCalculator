'use client';

import React, { useState } from 'react';
import { startCheckout } from '../lib/licence';
import dynamic from 'next/dynamic';

// The sign-in form is only rendered once the offer is opened, so its code (and
// the Supabase client behind it) is only fetched then.
const AuthUI = dynamic(() => import('./AuthUI').then((m) => m.AuthUI), { ssr: false });
import { useProStatus } from '../lib/useProStatus';

/**
 * Subscription state and the way into it.
 *
 * Deliberately understated. This sits in a trading terminal, next to live
 * quotes, and a loud upsell would be the most prominent thing on a screen whose
 * job is pricing positions. It states what is gated, what it costs, and gets
 * out of the way.
 *
 * Note the copy says what happens, not how good it is: "unlocks multi-leg
 * strategies" rather than "unlock the full power of". The person reading this
 * is deciding whether they need spreads, and they know whether they do.
 */
export const ProPanel: React.FC = () => {
  const { licence, pro, claiming, error, activate, deactivate, setError } = useProStatus();
  const [key, setKey] = useState('');
  const [busy, setBusy] = useState(false);
  const [showActivate, setShowActivate] = useState(false);
  // The offer is one line until asked for. It used to be rendered open under
  // the strategy list, so a visitor who had never touched a control met three
  // paragraphs of sales copy and an email/password form first, and the list it
  // was crowding out was left a 2-3 row scroller.
  const [open, setOpen] = useState(false);

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

  if (claiming) {
    return (
      <div style={panel}>
        <span style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)' }}>
          Activating your subscription…
        </span>
      </div>
    );
  }

  if (pro && licence) {
    const renews = new Date(licence.expiresEpoch * 1000).toLocaleDateString(undefined, {
      year: 'numeric',
      month: 'short',
      day: 'numeric',
    });
    return (
      <div style={panel}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '0.5rem' }}>
          <span style={{ fontSize: 'var(--text-2xs)', fontWeight: 600, letterSpacing: '0.04em' }}>
            <span style={badge}>PRO</span> multi-leg strategies active
          </span>
          <button onClick={deactivate} style={linkBtn} title="Remove the licence from this browser">
            sign out
          </button>
        </div>
        <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-400)', marginTop: '0.25rem' }}>
          Valid to {renews}. Renews automatically with your subscription.
        </div>
      </div>
    );
  }

  return (
    <div style={panel}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '0.5rem' }}>
        <span style={{ fontSize: 'var(--text-2xs)', fontWeight: 600 }}>
          Single-leg calls and puts are free
        </span>
        <button
          onClick={() => setOpen((v) => !v)}
          aria-expanded={open}
          aria-controls="pro-offer"
          style={linkBtn}
        >
          Pro &amp; sign in {open ? '▴' : '▾'}
        </button>
      </div>

      {open && (
        <div id="pro-offer" style={{ marginTop: '0.4375rem' }}>
          <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)', marginBottom: '0.4375rem' }}>
            Spreads, straddles, condors, butterflies and futures spreads need Pro. 7 days free, then
            $9.99/month or $99/year.
          </div>

          <div style={{ display: 'flex', gap: '0.25rem', flexWrap: 'wrap' }}>
            <button onClick={() => go('monthly')} disabled={busy} style={primaryBtn}>
              {busy ? '…' : 'Start free trial'}
            </button>
            <button onClick={() => go('annual')} disabled={busy} style={secondaryBtn}>
              Annual
            </button>
            <button onClick={() => setShowActivate((v) => !v)} style={linkBtn}>
              have a key?
            </button>
          </div>

          {/* Signing in is the PRIMARY way Pro arrives, not an afterthought. The
              subscription writes the tier onto the account, so a subscriber who
              signs in has Pro on every device with nothing to paste. The licence
              key below is the fallback for someone who paid before they had an
              account. */}
          <div style={{ marginTop: '0.5rem', paddingTop: '0.5rem', borderTop: '1px solid var(--color-line)' }}>
            <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-ink-300)', marginBottom: '0.375rem' }}>
              Already subscribed? Sign in and Pro follows your account.
            </div>
            <AuthUI />
          </div>

          {showActivate && (
            <div style={{ display: 'flex', gap: '0.25rem', marginTop: '0.4375rem' }}>
              <input
                value={key}
                onChange={(e) => setKey(e.target.value)}
                placeholder="lk_live_…"
                aria-label="Licence key"
                spellCheck={false}
                style={{
                  flex: 1,
                  minWidth: 0,
                  padding: '0.1875rem 0.375rem',
                  fontSize: 'var(--text-2xs)',
                  fontFamily: 'var(--font-mono, monospace)',
                  background: 'var(--color-base-800)',
                  border: '1px solid var(--color-line)',
                  color: 'inherit',
                }}
              />
              <button
                onClick={() => {
                  if (activate(key)) setKey('');
                }}
                style={secondaryBtn}
              >
                Activate
              </button>
            </div>
          )}
        </div>
      )}

      {error && (
        <div style={{ fontSize: 'var(--text-2xs)', color: 'var(--color-loss, #e5484d)', marginTop: '0.3125rem' }}>
          {error}
        </div>
      )}
    </div>
  );
};

const panel: React.CSSProperties = {
  padding: '0.5rem 0.625rem',
  borderTop: '1px solid var(--color-line)',
  background: 'var(--color-base-700)',
};

const badge: React.CSSProperties = {
  display: 'inline-block',
  padding: '0 0.25rem',
  marginRight: '0.25rem',
  background: 'var(--color-accent)',
  color: '#fff',
  fontSize: 'var(--text-2xs)',
  fontWeight: 700,
  letterSpacing: '0.06em',
};

const primaryBtn: React.CSSProperties = {
  padding: '0.1875rem 0.5rem',
  fontSize: 'var(--text-2xs)',
  fontWeight: 600,
  background: 'var(--color-accent)',
  color: '#fff',
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

const linkBtn: React.CSSProperties = {
  padding: '0.1875rem 0.25rem',
  minHeight: '1.5rem',
  fontSize: 'var(--text-2xs)',
  background: 'transparent',
  color: 'var(--color-ink-400)',
  border: 'none',
  cursor: 'pointer',
  textDecoration: 'underline',
};

export default ProPanel;
