'use client';

import React from 'react';
import { CurrencySelect } from './CurrencySelect';
import ThemeToggle from './ThemeToggle';
import Link from 'next/link';
import { usePathname } from 'next/navigation';


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

/**
 * Where a tab goes FROM the current page.
 *
 * The Calculator tab went to `/` from everywhere, so a reader of the Iron Condor
 * guide who pressed it lost the structure they were reading about and landed on
 * a generic workspace with nothing selected. From a guide it now goes to that
 * strategy's own calculator page, which is the screen the article is the
 * companion to. Everywhere else it is unchanged.
 */
export function tabHref(tab: (typeof TABS)[number], pathname: string): string {
  if (tab.label !== 'Calculator') return tab.href;
  const guide = pathname.match(/^\/guides\/([^/]+)\/?$/);
  return guide ? `/calculator/${guide[1]}` : tab.href;
}

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
    <nav aria-label="Site" className="site-nav">
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
              href={tabHref(tab, pathname)}
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
        A spacer, and nothing else. This slot used to read "Ad-free tool", which
        was the site's internal reason for the tool screens carrying no ads
        printed as if it were a feature a visitor had asked about -- and, as its
        own comment recorded, it was a small lie on any route that was not the
        tool. Which screens carry advertising is a property of the routes, not
        something a nav bar needs to announce.
      */}
      <span className="site-nav-spacer" aria-hidden="true" />

      {/*
        Sign in, with an ADDRESS.

        `AuthUI` was reachable only from inside ProPanel, inside
        StrategySelector -- so signing in, signing up and resetting a password
        all required finding a form nested two components deep in the
        calculator, and the header offered none of them. A reset that exists
        and cannot be found is indistinguishable from one that does not exist,
        which is how it was reported.

        Placed after the `marginLeft: auto` spacer so it sits as account chrome
        on the right rather than as a third content tab beside Calculator and
        Guides. Carries no height of its own, so `--nav-h` is unchanged and the
        workspace's `calc(100vh - var(--nav-h))` still agrees with it.
      */}
      <Link
        href="/auth/sign-in"
        aria-current={isActive('/auth/sign-in') ? 'page' : undefined}
        style={{
          fontSize: 'var(--text-2xs)',
          padding: '0.25rem 0.5rem',
          borderRadius: 'var(--radius-sm)',
          textDecoration: 'none',
          whiteSpace: 'nowrap',
          color: isActive('/auth/sign-in') ? 'var(--color-ink-100)' : 'var(--color-ink-300)',
          background: isActive('/auth/sign-in') ? 'var(--color-base-600)' : 'transparent',
        }}
      >
        Sign in
      </Link>

      {/*
        The display-currency picker. Formatting only -- see CurrencySelect for
        why that limit is stated in the tooltip rather than left implied.
      */}
      {/*
        The colour theme is a preference about the SITE, so it lives with the
        other two site-wide preferences (sign-in and display currency) rather
        than in the calculator's header, where it cost the symbol search a
        hundred pixels and pushed Recalculate onto a second row.
      */}
      <ThemeToggle />

      <CurrencySelect />
    </nav>
  );
}

export default SiteNav;
