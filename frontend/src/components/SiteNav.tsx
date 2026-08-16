'use client';

import React from 'react';
import Link from 'next/link';
import { usePathname } from 'next/navigation';
import { noAdsOnRoute } from '@/config/ad-routes';

/**
 * Site-level tabs, modelled on mortgagefvcalculator.com's header.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Deliberately slim. The calculator below it is pinned to the viewport and its
 * panels were just given back height that an empty ad placeholder had been
 * holding; a tall marketing header would take that straight back. It declares
 * its own height as `--nav-h` in globals.css and the workspace subtracts
 * exactly that, so the two cannot drift.
 *
 * Not rendered on `/widget`. That page is iframed into other people's sites and
 * must carry the calculator and nothing else — site chrome inside somebody
 * else's embed is both wrong and a way to leak navigation out of the frame.
 */

const TABS = [
  { href: '/', label: 'Calculator' },
  { href: '/guides', label: 'Guides' },
] as const;

export function SiteNav() {
  const pathname = usePathname() || '/';

  // The embed carries no chrome. Reuses the ad-suppression list's own notion of
  // "this is the widget" rather than re-testing the path, so the two agree.
  if (pathname === '/widget' || pathname.startsWith('/widget/')) return null;

  const isActive = (href: string) =>
    href === '/'
      ? pathname === '/' || pathname.startsWith('/calculator')
      : pathname === href || pathname.startsWith(`${href}/`);

  return (
    <nav
      aria-label="Site"
      style={{
        height: 'var(--nav-h)',
        display: 'flex',
        alignItems: 'center',
        gap: '0.75rem',
        padding: '0 0.75rem',
        borderBottom: '1px solid var(--color-line)',
        background: 'var(--color-base-800)',
        flex: 'none',
      }}
    >
      <Link
        href="/"
        style={{
          fontFamily: 'var(--font-fraunces), Georgia, serif',
          fontSize: '0.875rem',
          fontWeight: 700,
          color: 'var(--color-ink-100)',
          textDecoration: 'none',
          whiteSpace: 'nowrap',
        }}
      >
        Options &amp; Futures Calculator
      </Link>

      <div style={{ display: 'flex', gap: '0.25rem', marginLeft: '0.5rem' }}>
        {TABS.map((tab) => {
          const active = isActive(tab.href);
          return (
            <Link
              key={tab.href}
              href={tab.href}
              aria-current={active ? 'page' : undefined}
              style={{
                fontSize: 'var(--text-2xs)',
                padding: '0.25rem 0.5rem',
                borderRadius: 'var(--radius-sm)',
                textDecoration: 'none',
                whiteSpace: 'nowrap',
                color: active ? 'var(--color-ink-100)' : 'var(--color-ink-300)',
                background: active ? 'var(--color-base-600)' : 'transparent',
                fontWeight: active ? 600 : 400,
              }}
            >
              {tab.label}
            </Link>
          );
        })}
      </div>

      {/*
        Says which screens carry advertising, on the screens that do not.
        Cheap honesty, and it is the distinction this whole structure rests on:
        the tool screens serve no Google ads precisely because they carry no
        publisher content, and the guides carry both.
      */}
      <span
        style={{
          marginLeft: 'auto',
          fontSize: 'var(--text-2xs)',
          color: 'var(--color-ink-400)',
          whiteSpace: 'nowrap',
        }}
      >
        {noAdsOnRoute(pathname) ? 'Ad-free tool' : null}
      </span>
    </nav>
  );
}

export default SiteNav;
