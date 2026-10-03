'use client';

import React, { useCallback, useEffect, useState } from 'react';
import { createClient } from '../lib/supabase/client';

/**
 * Set a new password from a recovery link.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This site had NO password reset at all: `resetPasswordForEmail` appeared
 * nowhere in the bundle, the app router carried no `auth` route, and GoTrue --
 * which has SMTP configured and working -- defaulted its recovery path to
 * SITE_URL + "/", the home page. A reset link therefore verified, signed the
 * person in, and left them on the calculator with no way to choose a password.
 * Nothing errored, which is why it read as "the link does not work".
 *
 * THREE LINK SHAPES REACH HERE AND ONLY ONE OF THEM IS THE COMMON ONE. Which
 * arrives is decided by GoTrue's template and by the client's flow type, not by
 * anything this file controls, so all three are handled rather than assumed:
 *
 *   - `#access_token=...&type=recovery`  the implicit shape GoTrue's own
 *     /verify endpoint redirects with. supabase-js consumes it before this
 *     component mounts and fires PASSWORD_RECOVERY.
 *   - `?code=...`                        PKCE. Needs exchangeCodeForSession.
 *   - `?token_hash=...&type=recovery`    the newer template. Needs verifyOtp.
 *
 * Guessing one and shipping it would have produced a form that is permanently
 * dead for the other two, with the same silent signature as the defect it
 * replaces.
 *
 * The form is GATED on a recovery session existing. Rendering the inputs
 * unconditionally would let someone type a new password and then be told it
 * failed, where the real answer is that the link expired and they need another.
 */
export const ResetPasswordForm: React.FC = () => {
  const supabase = createClient();

  const [ready, setReady] = useState(false);
  const [checking, setChecking] = useState(true);
  const [password, setPassword] = useState('');
  const [confirm, setConfirm] = useState('');
  const [busy, setBusy] = useState(false);
  const [done, setDone] = useState(false);
  // Supabase returns failures in the RESOLVED value rather than throwing, so an
  // `await` that never reads `.error` discards every one. AuthUI.tsx carries the
  // same note: until it was added there, a wrong password did nothing at all.
  const [message, setMessage] = useState<{ kind: 'error' | 'info'; text: string } | null>(null);

  useEffect(() => {
    let cancelled = false;

    // Fires when supabase-js is the one that parsed the fragment. Subscribed
    // FIRST so the window between mount and the async checks below is covered.
    const {
      data: { subscription },
    } = supabase.auth.onAuthStateChange((event) => {
      if (cancelled) return;
      if (event === 'PASSWORD_RECOVERY' || event === 'SIGNED_IN') {
        setReady(true);
        setChecking(false);
      }
    });

    const establish = async () => {
      // An already-hydrated session is the implicit-fragment case, and the one
      // that happens in practice.
      const { data } = await supabase.auth.getSession();
      if (cancelled) return;
      if (data.session) {
        setReady(true);
        setChecking(false);
        return;
      }

      const params = new URLSearchParams(window.location.search);
      const code = params.get('code');
      const tokenHash = params.get('token_hash');

      if (code) {
        const { error } = await supabase.auth.exchangeCodeForSession(code);
        if (cancelled) return;
        if (!error) {
          setReady(true);
          setChecking(false);
          return;
        }
      } else if (tokenHash) {
        const { error } = await supabase.auth.verifyOtp({
          token_hash: tokenHash,
          type: 'recovery',
        });
        if (cancelled) return;
        if (!error) {
          setReady(true);
          setChecking(false);
          return;
        }
      }

      // Nothing established a session. Say which thing is wrong, because "it
      // did not work" is what sent this report in the first place.
      setChecking(false);
      setMessage({
        kind: 'error',
        text:
          'This reset link is no longer valid. Recovery links expire, and each one can ' +
          'only be used once. Request a new one from the sign-in form.',
      });
    };

    void establish();

    return () => {
      cancelled = true;
      subscription.unsubscribe();
    };
  }, [supabase]);

  const submit = useCallback(
    async (e: React.FormEvent) => {
      e.preventDefault();
      setMessage(null);

      // Checked here as well as by the server, because GoTrue's own refusal for
      // a short password is generic and arrives after a round trip.
      if (password.length < 8) {
        setMessage({ kind: 'error', text: 'Use at least 8 characters.' });
        return;
      }
      if (password !== confirm) {
        setMessage({ kind: 'error', text: 'Those two passwords are different.' });
        return;
      }

      setBusy(true);
      const { error } = await supabase.auth.updateUser({ password });
      setBusy(false);

      if (error) {
        setMessage({ kind: 'error', text: error.message });
        return;
      }
      setDone(true);
      setMessage({ kind: 'info', text: 'Password updated. You are signed in.' });
    },
    [password, confirm, supabase],
  );

  if (checking) {
    return <p className="text-sm text-[var(--color-ink-300)]">Checking your reset link…</p>;
  }

  if (done) {
    return (
      <div className="flex flex-col gap-3" role="status">
        <p className="text-sm">Your password has been changed and you are signed in.</p>
        <a href="/" className="btn w-fit">
          Back to the calculator
        </a>
      </div>
    );
  }

  if (!ready) {
    return (
      <div className="flex flex-col gap-3">
        {message && (
          <p className="text-sm" style={{ color: 'var(--color-loss)' }} role="alert">
            {message.text}
          </p>
        )}
        <a href="/" className="btn w-fit">
          Back to the calculator
        </a>
      </div>
    );
  }

  return (
    <form onSubmit={submit} className="flex flex-col gap-3">
      <label className="flex flex-col gap-1 text-sm">
        New password
        <input
          type="password"
          autoComplete="new-password"
          className="p-2 rounded bg-transparent border border-white/20 text-sm"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
          minLength={8}
        />
      </label>
      <label className="flex flex-col gap-1 text-sm">
        Confirm new password
        <input
          type="password"
          autoComplete="new-password"
          className="p-2 rounded bg-transparent border border-white/20 text-sm"
          value={confirm}
          onChange={(e) => setConfirm(e.target.value)}
          required
          minLength={8}
        />
      </label>
      <button type="submit" className="btn w-fit" disabled={busy}>
        {busy ? 'Saving…' : 'Set new password'}
      </button>
      {message && (
        <span
          className="text-xs"
          style={{
            color: message.kind === 'error' ? 'var(--color-loss)' : 'var(--color-ink-300)',
          }}
          role={message.kind === 'error' ? 'alert' : 'status'}
        >
          {message.text}
        </span>
      )}
    </form>
  );
};
