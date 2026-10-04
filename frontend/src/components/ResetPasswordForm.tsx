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
  // For the request half, reached when no recovery session is established.
  const [email, setEmail] = useState('');
  const [sending, setSending] = useState(false);
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

      // THE IMPLICIT FRAGMENT, AND THE REASON THIS IS DONE BY HAND.
      //
      // GoTrue's /verify endpoint 303s to this page with the session in the URL
      // FRAGMENT: `#access_token=...&refresh_token=...&type=recovery`. A client
      // built by supabase-js's own `createClient` consumes that itself under
      // `detectSessionInUrl`. `createBrowserClient` from @supabase/ssr -- which
      // is what this app uses, and must, because the rest of the site reads the
      // session from cookies -- is the PKCE/cookie client and DOES NOT. So
      // `getSession()` stays null, there is no `?code` or `?token_hash` to
      // exchange, PASSWORD_RECOVERY never fires, and a perfectly valid link
      // renders "this link is no longer valid".
      //
      // Measured in a real browser against production before this was added:
      // the page carried a valid recovery JWT in the fragment and still showed
      // the expired message. Every HTTP-level check passed -- the 303, the
      // redirect target, the token -- because the failure is entirely inside the
      // client library's choice of flow.
      const hash = new URLSearchParams(window.location.hash.replace(/^#/, ''));
      const hashAccess = hash.get('access_token');
      const hashRefresh = hash.get('refresh_token');
      if (hashAccess && hashRefresh) {
        const { error } = await supabase.auth.setSession({
          access_token: hashAccess,
          refresh_token: hashRefresh,
        });
        if (cancelled) return;
        if (!error) {
          // Drop the tokens from the address bar. They are single-use and
          // already spent, and leaving them there puts a credential in history
          // and in anything the browser syncs.
          window.history.replaceState(null, '', window.location.pathname);
          setReady(true);
          setChecking(false);
          return;
        }
        setChecking(false);
        setMessage({ kind: 'error', text: error.message });
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

  const requestLink = useCallback(
    async (e: React.FormEvent) => {
      e.preventDefault();
      setMessage(null);
      setSending(true);
      try {
        const { error } = await supabase.auth.resetPasswordForEmail(email, {
          redirectTo: `${window.location.origin}/auth/reset-password`,
        });
        if (error) {
          // GoTrue rate-limits one email per address per minute, and its own
          // message says so. Surfaced as INFO rather than as an error, because
          // "you can only request this once every 60 seconds" means the first
          // request worked -- reporting it in loss red reads as a failure to
          // send, which is the opposite of what happened.
          const limited = /once every|rate limit/i.test(error.message);
          setMessage({ kind: limited ? 'info' : 'error', text: error.message });
        } else {
          // Identical whether or not the address has an account: GoTrue answers
          // 200 either way so this endpoint cannot enumerate who signed up.
          setMessage({
            kind: 'info',
            text: 'If that email has an account, a reset link is on its way. It expires, and each link works once.',
          });
        }
      } catch {
        setMessage({ kind: 'error', text: 'Could not reach the sign-in service.' });
      } finally {
        setSending(false);
      }
    },
    [email, supabase],
  );

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
    // NOT a dead end. This branch is reached two ways -- an expired or
    // already-used link, and someone who simply navigated here -- and in both
    // cases the next thing they need is a new link. Telling them to "request
    // one from the sign-in form" sent them hunting for a form nested two
    // components deep inside the calculator, which is how the reset ended up
    // unreachable in the first place. So the request lives here too.
    return (
      <form onSubmit={requestLink} className="flex flex-col gap-3">
        {message && (
          <p
            className="text-sm"
            style={{
              color: message.kind === 'error' ? 'var(--color-loss)' : 'var(--color-ink-300)',
            }}
            role={message.kind === 'error' ? 'alert' : 'status'}
          >
            {message.text}
          </p>
        )}
        <label className="flex flex-col gap-1 text-sm">
          Your email
          <input
            type="email"
            autoComplete="email"
            className="p-2 rounded bg-transparent border border-white/20 text-sm"
            value={email}
            onChange={(e) => setEmail(e.target.value)}
            required
          />
        </label>
        <button type="submit" className="btn w-fit" disabled={sending}>
          {sending ? 'Sending…' : 'Email me a reset link'}
        </button>
        <a href="/" className="text-xs underline text-[var(--color-ink-400)]">
          Back to the calculator
        </a>
      </form>
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
