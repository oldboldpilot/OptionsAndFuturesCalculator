import React from 'react';
import { Metadata } from 'next';
import { branding } from '@/config/branding';
import { pageTitle } from '@/lib/seo-title';
import { AuthUI } from '@/components/AuthUI';

export const metadata: Metadata = {
  title: pageTitle('Sign in'),
  description: `Sign in to ${branding.appName}, or reset your password.`,
  robots: { index: false, follow: false },
  alternates: { canonical: `${branding.canonicalUrl}/auth/sign-in` },
};

/**
 * A sign-in screen with an ADDRESS.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `AuthUI` -- which carries sign-in, sign-up and "Forgot password?" -- was
 * rendered in exactly ONE place: inside `ProPanel`, which is rendered inside
 * `StrategySelector`. So every one of those three actions required finding a
 * small form nested two components deep in the calculator, and the site header
 * offered no sign-in affordance at all. The password reset existed and was
 * unreachable, which is indistinguishable from not existing. `ProPanel`'s own
 * comment records the earlier form of the same defect: "Until now this
 * component was never imported anywhere, so there was no way to sign in at
 * all."
 *
 * This page adds no auth logic. It renders the SAME `AuthUI` component so
 * there is one implementation of signing in -- two would agree the day they
 * were written and drift on the first change to either.
 *
 * noindex: a sign-in form is not publisher content and nothing should rank it.
 * It is outside /guides/, so config/ad-routes.ts gives it no ad code.
 */
export default function SignInPage() {
  return (
    <main className="mx-auto flex min-h-[60vh] w-full max-w-md flex-col justify-center gap-4 px-4 py-12">
      <h1 className="text-xl font-semibold">Sign in</h1>
      <p className="text-sm text-[var(--color-ink-300)]">
        Signing in carries your Pro entitlement across devices. Forgotten your password? Enter
        your email below and choose <strong>Forgot password?</strong>
      </p>
      <AuthUI />
      <p className="text-xs text-[var(--color-ink-400)]">
        Already have a reset link?{' '}
        <a href="/auth/reset-password" className="underline">
          Set a new password
        </a>
        .
      </p>
    </main>
  );
}
