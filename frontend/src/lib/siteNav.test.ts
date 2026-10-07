/**
 * The Calculator tab follows the guide you are reading (OFC-028).
 *
 * @author Olumuyiwa Oluwasanmi
 */
import { describe, it, expect } from 'vitest';
import { tabHref } from '../components/SiteNav';

const calc = { href: '/', label: 'Calculator' } as const;
const guides = { href: '/guides', label: 'Guides' } as const;

describe('tabHref', () => {
  it('sends the Calculator tab from a guide to that strategy\'s calculator', () => {
    expect(tabHref(calc, '/guides/iron-condor')).toBe('/calculator/iron-condor');
    expect(tabHref(calc, '/guides/iron-condor/')).toBe('/calculator/iron-condor');
  });

  it('leaves the Calculator tab alone everywhere else', () => {
    for (const p of ['/', '/guides', '/privacy', '/calculator/long-call', '/widget', '/nope']) {
      expect(tabHref(calc, p), p).toBe('/');
    }
  });

  it('never rewrites the Guides tab', () => {
    expect(tabHref(guides, '/guides/iron-condor')).toBe('/guides');
    expect(tabHref(guides, '/')).toBe('/guides');
  });
});
