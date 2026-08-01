import React from 'react';
import { Metadata } from 'next';
import { branding } from '@/config/branding';
import { LegalPage, H2, P, UL } from '@/components/LegalPage';

export const metadata: Metadata = {
  title: 'Privacy Policy',
  description: `How ${branding.appName} handles your data, cookies and advertising.`,
  alternates: { canonical: `${branding.canonicalUrl}/privacy` },
};

/**
 * Required, not decorative.
 *
 * Google AdSense will not approve a site that serves ads without a privacy
 * policy disclosing third-party cookie use, and this site had no policy page at
 * all -- /privacy, /terms, /about and /contact were all 404.
 *
 * Written from what the application actually does rather than from a template:
 * every claim below corresponds to something in this codebase, because a policy
 * that describes behaviour the site does not have is worse than none.
 */
export default function PrivacyPage() {
  return (
    <LegalPage title="Privacy Policy" updated="1 August 2026">
      <P>
        {branding.appName} is a calculator for options and futures strategies. This page
        describes what it collects, why, and who else receives it.
      </P>

      <H2>What we collect</H2>
      <UL
        items={[
          <>
            <strong>Nothing, if you only use the calculator.</strong> Strategy inputs — symbols,
            strikes, expiries, quantities — are sent to our pricing service to be computed and are
            not stored against you or used to build a profile.
          </>,
          <>
            <strong>An email address and password, if you create an account.</strong> Authentication
            is handled by our self-hosted Supabase instance. Passwords are stored only as a hash;
            we cannot read them.
          </>,
          <>
            <strong>Your subscription status, if you subscribe.</strong> Payment is processed by
            Stripe. Card details go to Stripe directly and never reach our servers. We store which
            plan you hold and when it renews.
          </>,
        ]}
      />

      <H2>Storage in your browser</H2>
      <P>
        We use <code>localStorage</code> to keep you signed in and to remember an activated
        subscription licence on this device. It is not a tracking identifier, it is not sent to
        third parties, and clearing your browser data removes it.
      </P>

      <H2>Advertising</H2>
      <P>
        This site shows advertising supplied by Google. Specifically:
      </P>
      <UL
        items={[
          <>
            Third-party vendors, including Google, use cookies to serve ads based on your prior
            visits to this and other websites.
          </>,
          <>
            Google&rsquo;s use of advertising cookies enables it and its partners to serve ads to
            you based on your visit to this and other sites.
          </>,
          <>
            You can opt out of personalised advertising by visiting{' '}
            <a href="https://www.google.com/settings/ads" rel="noopener noreferrer" target="_blank">
              Google Ads Settings
            </a>
            . You can opt out of third-party vendors&rsquo; use of cookies for personalised
            advertising at{' '}
            <a href="https://www.aboutads.info/choices/" rel="noopener noreferrer" target="_blank">
              aboutads.info/choices
            </a>
            .
          </>,
        ]}
      />

      <H2>Who else receives data</H2>
      <UL
        items={[
          <><strong>Google AdSense</strong> — advertising, as described above.</>,
          <><strong>Stripe</strong> — payment processing, if you subscribe.</>,
          <>
            <strong>Cloudflare</strong> — serves this site and provides aggregate traffic
            statistics. Cloudflare Web Analytics does not use cookies and does not fingerprint
            individuals.
          </>,
          <>
            <strong>Alpaca</strong> — supplies the market data shown. Requests for quotes and option
            chains go through our own service; Alpaca does not receive anything identifying you.
          </>,
        ]}
      />
      <P>We do not sell personal data, and we have nothing to sell — we hold very little of it.</P>

      <H2>Your rights</H2>
      <P>
        If you hold an account you can ask us to export or delete it, and deletion removes the
        account and its saved strategies. Email{' '}
        <a href="mailto:privacy@optionsandfuturescalculator.com">
          privacy@optionsandfuturescalculator.com
        </a>
        . If you are in the UK or EU, the lawful basis for holding your account details is
        performance of a contract — we cannot provide an account without them.
      </P>

      <H2>Children</H2>
      <P>
        This site is not directed at children under 13 and we do not knowingly collect their data.
      </P>

      <H2>Changes</H2>
      <P>
        Material changes will be reflected in the date at the top of this page. This site is operated
        by Knobugsoft LLC, which is the data controller for the purposes of UK and EU data
        protection law.
      </P>
    </LegalPage>
  );
}
