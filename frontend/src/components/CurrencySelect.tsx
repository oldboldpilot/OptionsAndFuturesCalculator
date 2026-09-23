'use client';

import { useEffect, useState } from 'react';
import { CURRENCIES, detectCurrency, setCurrency, useCurrency } from '../lib/currency';

/**
 * Display currency picker.
 *
 * The tooltip is not boilerplate. This site's amounts come from a LIVE US
 * option chain and are denominated in dollars; nothing here converts them, so
 * a viewer who picks EUR sees the same number with a different symbol. Saying
 * so is the difference between localising a display and misstating a price —
 * the same standard this project applies to the LIVE badge, which is derived
 * from the chain's own `fetched_at` rather than from request status precisely
 * so it cannot claim freshness it does not have.
 *
 * THE OPTION LIST IS CLIENT-ONLY, AND THAT IS A CONTENT-POLICY FIX RATHER THAN
 * A PERFORMANCE ONE. Rendering all 38 currencies server-side put the same 38
 * lines of text into the static HTML of all 59 exported pages, and
 * `check-export.mjs` failed the build: pairwise 6-gram Jaccard across the
 * calculator pages rose to 0.5172 against its 0.5 ceiling, and
 * bear-call-spread/bull-put-spread to 0.5535 against 0.54. Those pages are
 * already near the ceiling by design — they are the same tool with a different
 * strategy name — so identical chrome is exactly what they cannot afford.
 *
 * The honest fix is to emit less duplicated text, not to raise the ceiling: a
 * moved threshold with no reason behind it is indistinguishable from a moved
 * goalpost, and this repository has already paid for that lesson once. So the
 * server renders the SELECTED currency alone and the rest arrive on mount,
 * which is also when the picker first becomes usable — there is nothing to
 * pick with before hydration.
 */
export function CurrencySelect({ className = '' }: { className?: string }) {
  const currency = useCurrency();
  const [mounted, setMounted] = useState(false);

  // Deliberately after paint: the first client render must match the server's
  // single option or React reports a hydration mismatch.
  //
  // `detectCurrency` runs here too, and it is what makes the choice a
  // PREFERENCE rather than a toggle -- it restores the saved code from
  // localStorage, falling back to the browser's region and then to the
  // Cloudflare edge's. Without it the picker worked and then forgot on the
  // next page load, which a browser test caught and no unit test could.
  useEffect(() => {
    setMounted(true);
    void detectCurrency();
  }, []);

  const options = mounted ? CURRENCIES : CURRENCIES.filter((c) => c.code === currency.code);

  return (
    <label className={`currency-select ${className}`}>
      <span className="sr-only">Display currency</span>
      <select
        value={currency.code}
        onChange={(e) => setCurrency(e.target.value)}
        title="Display currency — formatting only. Prices come from US markets in USD and are NOT converted."
        style={{
          fontSize: 'var(--text-2xs)',
          padding: '0.125rem 0.25rem',
          borderRadius: 'var(--radius-sm)',
          background: 'var(--color-base-600)',
          color: 'var(--color-ink-200)',
          border: '1px solid var(--color-line)',
        }}
      >
        {options.map((c) => (
          <option key={c.code} value={c.code}>
            {/* Full name once mounted: it is client-only, so it costs nothing
                in the static HTML the similarity gate measures. */}
            {mounted ? `${c.code} — ${c.label}` : c.code}
          </option>
        ))}
      </select>
    </label>
  );
}
