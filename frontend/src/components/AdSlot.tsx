'use client';

import React, { useEffect, useRef, useState } from 'react';

/**
 * An advertising unit that reserves its space before it fills it.
 *
 * The box holds its own height from first paint, before any ad script runs. An
 * ad that sizes itself on arrival shoves the page down under the reader's
 * cursor — on a page where the thing below is a strike ladder with buy and sell
 * buttons, a late reflow is a misclick that opens a position. Reserving the box
 * is the difference between an ad and an accident.
 *
 * With no slot id configured the space stays empty and labelled rather than
 * rendering an `<ins>` AdSense will ignore — which would look like a working ad
 * that never fills. That fallback is what makes this safe to ship before the
 * units exist in the dashboard: the layout is already correct, and supplying
 * the id is the only remaining step.
 */

const CLIENT = 'ca-pub-3669553016263703';

type AdSize = 'leaderboard' | 'rectangle' | 'multiplex';

/**
 * Slot ids come from the AdSense dashboard, one per unit created there. They
 * cannot be minted from the API — AdSense's management API is read-only for ad
 * units — so they are configuration, not something the build can derive.
 *
 * They are not secrets: every site running ads exposes them in its page source.
 * NEXT_PUBLIC_ is therefore correct rather than a compromise.
 */
const SLOTS: Record<AdSize, string | undefined> = {
  leaderboard: process.env.NEXT_PUBLIC_ADSENSE_SLOT_LEADERBOARD,
  rectangle: process.env.NEXT_PUBLIC_ADSENSE_SLOT_RECTANGLE,
  // A real, issued unit, so it is a literal rather than an env var that a build
  // without .env.local would silently drop -- the same failure that left
  // canonicalUrl pointing at another domain. The slot id is not a secret; it is
  // visible in the page source of every site that runs ads.
  multiplex: process.env.NEXT_PUBLIC_ADSENSE_SLOT_MULTIPLEX || '7620187871',
};

declare global {
  interface Window {
    adsbygoogle?: unknown[];
  }
}

export function AdSlot({
  size = 'leaderboard',
  label = 'Advertisement',
}: {
  size?: AdSize;
  label?: string;
}) {
  // IAB standard units, so the reserved box matches what will occupy it.
  // Multiplex (autorelaxed) sizes itself to its container and grows to fit the
  // recommendation grid, so it gets no fixed box. The other two are IAB units
  // whose reserved space must match what will occupy it.
  const dims =
    size === 'leaderboard' ? { width: 728, height: 90 }
    : size === 'multiplex' ? { width: 0, height: 0 }
    : { width: 300, height: 250 };

  const slot = SLOTS[size];
  const pushed = useRef(false);
  // The <ins> is rendered only after mount. This is a static export served from
  // a CDN, so markup produced at build time must match the first client render;
  // injecting the ad tag during render would be a hydration mismatch.
  const [mounted, setMounted] = useState(false);

  useEffect(() => setMounted(true), []);

  useEffect(() => {
    if (!mounted || !slot || pushed.current) return;
    // Guarded with a ref because React runs effects twice in development, and a
    // second push against an <ins> that already carries an ad throws
    // "All ins elements in the DOM with class=adsbygoogle already have ads".
    pushed.current = true;
    try {
      (window.adsbygoogle = window.adsbygoogle || []).push({});
    } catch {
      // A blocked, failed or ad-blocked unit must never take the calculator
      // down with it. There is nothing to recover — the reserved box stays
      // empty, which is exactly what it looked like a moment earlier.
    }
  }, [mounted, slot]);

  // Multiplex must not be clamped: `maxWidth: 0` would collapse it to nothing,
  // and a fixed height would crop the recommendation grid it exists to show. It
  // is the one format here that legitimately sizes itself.
  const isFluid = size === 'multiplex';
  const frame: React.CSSProperties = {
    width: '100%',
    maxWidth: isFluid ? '100%' : dims.width,
    minHeight: isFluid ? 200 : dims.height,
    margin: '0 auto',
    flex: 'none',
  };

  if (!slot) {
    return (
      <aside
        aria-label={label}
        style={{
          ...frame,
          height: isFluid ? 200 : dims.height,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          border: '1px dashed var(--color-line)',
          borderRadius: 'var(--radius-sm)',
          background: 'var(--color-base-700)',
          color: 'var(--color-ink-400)',
          fontSize: 'var(--text-2xs)',
          letterSpacing: '0.08em',
          textTransform: 'uppercase',
        }}
      >
        {label}
      </aside>
    );
  }

  return (
    <aside aria-label={label} style={frame}>
      {mounted && (
        <ins
          className="adsbygoogle"
          style={
            isFluid
              ? { display: 'block' }
              : { display: 'block', width: '100%', height: dims.height }
          }
          data-ad-client={CLIENT}
          data-ad-slot={slot}
          data-ad-format={
            size === 'leaderboard' ? 'horizontal'
            : size === 'multiplex' ? 'autorelaxed'
            : 'rectangle'
          }
          data-full-width-responsive="true"
        />
      )}
    </aside>
  );
}

export default AdSlot;
