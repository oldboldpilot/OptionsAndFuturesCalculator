import React from 'react';
import Link from 'next/link';

/**
 * Shared shell for the policy pages.
 *
 * These are the only pages on the site meant to be READ rather than operated,
 * so they deliberately drop the terminal's density: a measured column, ordinary
 * line height, and no live data. A privacy policy set in 11px monospace at
 * terminal line spacing is compliant and unreadable, which defeats the point of
 * having one.
 */
export function LegalPage({
  title,
  updated,
  children,
}: {
  title: string;
  updated: string;
  children: React.ReactNode;
}) {
  return (
    <main
      style={{
        maxWidth: '46rem',
        margin: '0 auto',
        padding: '3rem 1.5rem 5rem',
        color: 'var(--color-ink-200)',
        fontSize: '0.9375rem',
        lineHeight: 1.7,
      }}
    >
      <Link
        href="/"
        style={{ fontSize: '0.8125rem', color: 'var(--color-accent)', textDecoration: 'none' }}
      >
        ← Back to the calculator
      </Link>

      <h1
        style={{
          fontFamily: 'var(--font-display), Georgia, serif',
          fontSize: 'clamp(1.75rem, 4vw, 2.5rem)',
          lineHeight: 1.15,
          margin: '1.5rem 0 0.375rem',
          color: 'var(--color-ink-100)',
        }}
      >
        {title}
      </h1>
      <p style={{ fontSize: '0.8125rem', color: 'var(--color-ink-400)', marginBottom: '2.5rem' }}>
        Last updated {updated}
      </p>

      {children}

      <hr style={{ border: 0, borderTop: '1px solid var(--color-line)', margin: '3rem 0 1.5rem' }} />
      <nav style={{ display: 'flex', gap: '1.25rem', fontSize: '0.8125rem' }}>
        <Link href="/privacy" style={{ color: 'var(--color-ink-400)' }}>
          Privacy
        </Link>
        <Link href="/terms" style={{ color: 'var(--color-ink-400)' }}>
          Terms
        </Link>
        <a
          href="mailto:hello@optionsandfuturescalculator.com"
          style={{ color: 'var(--color-ink-400)' }}
        >
          Contact
        </a>
      </nav>
    </main>
  );
}

export function H2({ children }: { children: React.ReactNode }) {
  return (
    <h2
      style={{
        fontSize: '1.0625rem',
        fontWeight: 600,
        margin: '2.25rem 0 0.625rem',
        color: 'var(--color-ink-100)',
      }}
    >
      {children}
    </h2>
  );
}

export function P({ children }: { children: React.ReactNode }) {
  return <p style={{ margin: '0 0 1rem' }}>{children}</p>;
}

export function UL({ items }: { items: React.ReactNode[] }) {
  return (
    <ul style={{ margin: '0 0 1rem', paddingLeft: '1.25rem' }}>
      {items.map((item, i) => (
        <li key={i} style={{ margin: '0 0 0.5rem' }}>
          {item}
        </li>
      ))}
    </ul>
  );
}
