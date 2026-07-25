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
    if (isSignUp) {
      await supabase.auth.signUp({ email, password });
    } else {
      await supabase.auth.signInWithPassword({ email, password });
    }
  };

  const handleLogout = async () => {
    await supabase.auth.signOut();
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
              <button type="submit" className="btn">
                {isSignUp ? 'Sign Up' : 'Sign In'}
              </button>
            </div>
            <button
              type="button"
              className="text-xs text-white/70 hover:text-white"
              onClick={() => setIsSignUp(!isSignUp)}
            >
              {isSignUp ? 'Login instead' : 'Create account'}
            </button>
          </form>
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
        </div>
      )}
    </div>
  );
};
