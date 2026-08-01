import React from 'react';
import { Metadata } from 'next';
import { branding } from '@/config/branding';
import { LegalPage, H2, P, UL } from '@/components/LegalPage';

export const metadata: Metadata = {
  title: 'Terms of Use',
  description: `Terms of use for ${branding.appName}, including the financial disclaimer.`,
  alternates: { canonical: `${branding.canonicalUrl}/terms` },
};

/**
 * The disclaimer here is not boilerplate padding -- it is the single most
 * important statement on the site.
 *
 * This tool prices real instruments from live market data, and someone can act
 * on a number it shows. It is a calculator, not advice, and saying so plainly
 * matters both to the reader and to any advertising or payment provider
 * assessing what this site is.
 */
export default function TermsPage() {
  return (
    <LegalPage title="Terms of Use" updated="1 August 2026">
      <H2>Not financial advice</H2>
      <P>
        {branding.appName} is a calculator. It computes what a strategy would be worth under the
        inputs you give it. It does not know your circumstances, it does not recommend positions,
        and nothing it displays is advice, a solicitation, or an offer to trade.
      </P>
      <P>
        Options and futures carry substantial risk, including loss exceeding your initial outlay on
        short and leveraged positions. Decide with a qualified adviser, not with a payoff chart.
      </P>

      <H2>About the numbers</H2>
      <UL
        items={[
          <>
            Market data is supplied by third parties and may be delayed, incomplete or wrong. We
            show what we receive; we do not independently verify it.
          </>,
          <>
            Model outputs — Greeks, implied volatility, probabilities — depend on assumptions that
            do not hold exactly in real markets. They are estimates, not measurements.
          </>,
          <>
            Prices shown are indicative. They do not include commissions, fees, financing,
            assignment risk, or the spread you would actually pay.
          </>,
        ]}
      />
      <P>
        We make no warranty that the figures are accurate or fit for any purpose, and we are not
        liable for losses arising from their use.
      </P>

      <H2>Accounts</H2>
      <P>
        You are responsible for keeping your credentials secure and for activity under your account.
        Tell us promptly if you believe it has been compromised.
      </P>

      <H2>Subscriptions</H2>
      <UL
        items={[
          <>
            Single-leg calls and puts are free. Multi-leg strategies require a Pro subscription.
          </>,
          <>
            Subscriptions renew automatically until cancelled. Cancel any time; access continues to
            the end of the period you have paid for.
          </>,
          <>Payment is handled by Stripe under their terms.</>,
        ]}
      />

      <H2>Acceptable use</H2>
      <P>
        Do not attempt to disrupt the service, circumvent its limits, or scrape it at a volume that
        degrades it for others. Programmatic access is available under a separate API agreement.
      </P>

      <H2>Changes</H2>
      <P>
        We may update these terms; the date above reflects the last change. Continued use after a
        change means you accept it. This site is operated by Knobugsoft LLC.
      </P>

      <H2>Contact</H2>
      <P>
        <a href="mailto:hello@optionsandfuturescalculator.com">
          hello@optionsandfuturescalculator.com
        </a>
      </P>
    </LegalPage>
  );
}
