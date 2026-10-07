import React from 'react';
import { Metadata } from 'next';
import { branding } from '@/config/branding';
import { pageTitle } from '@/lib/seo-title';
import { ResetPasswordForm } from '@/components/ResetPasswordForm';

export const metadata: Metadata = {
  title: pageTitle('Set a new password'),
  description: `Choose a new password for your ${branding.appName} account.`,
  // noindex for the same reason not-found.tsx is: this is a screen a visitor
  // reaches from their own email, never from search, and it carries no
  // publisher content. It is also outside /guides/, so the ad allowlist in
  // config/ad-routes.ts gives it no ad code by construction -- which is what
  // keeps it clear of the "ads on a screen without publisher content" policy
  // that flagged this site once already.
  robots: { index: false, follow: false },
  alternates: { canonical: `${branding.canonicalUrl}/auth/reset-password` },
};

/**
 * The landing page for a GoTrue recovery link.
 *
 * A SERVER component wrapping a client one, which is the shape this app already
 * uses: the export is static, so the HTML a visitor receives has to exist at
 * build time, while the token handling needs the browser. Putting `'use client'`
 * on the page itself would also forbid this `metadata` export.
 *
 * The route is `/auth/reset-password` and GoTrue must be told so --
 * GOTRUE_MAILER_URLPATHS_RECOVERY on the supabase-auth service. Shipping this
 * page without that variable leaves the emails pointing at the home page, and
 * shipping the variable without this page gives every reset link a 404. The
 * page goes first.
 */
export default function ResetPasswordPage() {
  return (
    <main className="mx-auto flex min-h-[60vh] w-full max-w-md flex-col justify-center gap-4 px-4 py-12">
      <h1 className="text-xl font-semibold">Set a new password</h1>
      <p className="text-sm text-[var(--color-ink-300)]">
        You opened a password reset link for {branding.appName}. Choose a new password below.
      </p>
      <ResetPasswordForm />
    </main>
  );
}
