'use client';
import React, { useState, useEffect } from 'react';
import { createClient } from '../lib/supabase/client';
import { type User } from '@supabase/supabase-js';

export const AuthUI: React.FC = () => {
  const [user, setUser] = useState<User | null>(null);
  const [loading, setLoading] = useState(true);
  const supabase = createClient();

  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [isSignUp, setIsSignUp] = useState(false);
  // Supabase returns errors in the resolved value rather than throwing, so
  // `await`ing without reading `.error` discards every failure. Until this was
  // added, a wrong password did nothing at all -- the form simply sat there,
  // which reads as a broken site rather than as a rejected credential.
  const [message, setMessage] = useState<{ kind: 'error' | 'info'; text: string } | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    supabase.auth.getSession().then(({ data: { session } }) => {
      setUser(session?.user ?? null);
      setLoading(false);
    });

    const {
      data: { subscription },
    } = supabase.auth.onAuthStateChange((_event, session) => {
      setUser(session?.user ?? null);
    });

    return () => subscription.unsubscribe();
  }, [supabase.auth]);

  const handleAuth = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setMessage(null);
    try {
      if (isSignUp) {
        const { data, error } = await supabase.auth.signUp({ email, password });
        if (error) {
          setMessage({ kind: 'error', text: error.message });
        } else if (!data.session) {
          // Sign-up with email confirmation on returns a user but no session.
          // Saying nothing here looks like a failure, and the person goes on
          // waiting for something that already happened in their inbox.
          setMessage({ kind: 'info', text: 'Check your email to confirm your account.' });
        }
      } else {
        const { error } = await supabase.auth.signInWithPassword({ email, password });
        if (error) setMessage({ kind: 'error', text: error.message });
      }
    } catch {
      // A network failure or an unreachable auth host rejects rather than
      // resolving, so it needs catching separately from the error field.
      setMessage({ kind: 'error', text: 'Could not reach the sign-in service.' });
    } finally {
      setBusy(false);
    }
  };

  const handleLogout = async () => {
    const { error } = await supabase.auth.signOut();
    if (error) setMessage({ kind: 'error', text: error.message });
  };

  const handleOAuthSignIn = async (provider: 'google' | 'apple') => {
    await supabase.auth.signInWithOAuth({
      provider: provider,
      options: {
        redirectTo: `${window.location.origin}/auth/callback`,
      },
    });
  };

  if (loading) {
    return <div className="text-sm">Loading...</div>;
  }

  return (
    <div className="flex items-center gap-4">
      {user ? (
        <div className="flex items-center gap-4">
          <span className="text-sm font-medium">{user.email}</span>
          <button className="btn" onClick={handleLogout}>
            Sign Out
          </button>
        </div>
      ) : (
        <div className="flex items-center gap-4">
          <form onSubmit={handleAuth} className="flex flex-col gap-2">
            <div className="flex gap-2">
              <input
                type="email"
                placeholder="Email"
                className="p-1 rounded bg-transparent border border-white/20 text-sm"
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                required
              />
              <input
                type="password"
                placeholder="Password"
                className="p-1 rounded bg-transparent border border-white/20 text-sm"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                required
              />
              <button type="submit" className="btn" disabled={busy}>
                {busy ? '…' : isSignUp ? 'Sign Up' : 'Sign In'}
              </button>
            </div>
            <button
              type="button"
              className="text-xs text-white/70 hover:text-white"
              onClick={() => {
                setIsSignUp(!isSignUp);
                setMessage(null);
              }}
            >
              {isSignUp ? 'Login instead' : 'Create account'}
            </button>
            {message && (
              <span
                className="text-xs"
                style={{ color: message.kind === 'error' ? 'var(--color-loss)' : 'var(--color-ink-300)' }}
                role={message.kind === 'error' ? 'alert' : 'status'}
              >
                {message.text}
              </span>
            )}
          </form>
          {/* Hidden unless a provider is actually configured in GoTrue. These
              redirect to /auth/callback, which does not exist in a static
              export, and no GOTRUE_EXTERNAL_* provider is set -- so shown, they
              are two buttons that always fail. */}
          {process.env.NEXT_PUBLIC_OAUTH_ENABLED === '1' && (
          <div className="flex flex-col gap-2 border-l border-white/20 pl-4">
            <button
              type="button"
              className="text-xs bg-white/10 hover:bg-white/20 p-1.5 rounded flex items-center justify-center gap-2 transition-colors"
              onClick={() => handleOAuthSignIn('google')}
            >
              Sign in with Google
            </button>
            <button
              type="button"
              className="text-xs bg-white/10 hover:bg-white/20 p-1.5 rounded flex items-center justify-center gap-2 transition-colors"
              onClick={() => handleOAuthSignIn('apple')}
            >
              Sign in with Apple
            </button>
          </div>
          )}
        </div>
      )}
    </div>
  );
};
