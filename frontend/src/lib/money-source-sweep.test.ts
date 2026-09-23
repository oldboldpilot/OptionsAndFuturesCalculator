/**
 * @author Olumuyiwa Oluwasanmi
 *
 * A SOURCE SWEEP over every component, because money-format.test.ts cannot see
 * the defect that actually shipped.
 *
 * The formatter was never wrong on this site -- there WAS no formatter. What
 * was wrong was every component formatting money for itself: two copies of the
 * same `money()` helper, a ticket total built with `toFixed(2)`, grid cells
 * with `toFixed(0)`, and a chain rendering `5900.00` for a strike. A unit test
 * of a formatter passes perfectly while the screen is full of ungrouped
 * amounts, so the gate has to ask a different question: does anything render
 * an amount WITHOUT going through the one module?
 *
 * Same shape as check-export.mjs: sweep what exists rather than a hand-written
 * list, because a list maintained beside the code can only ask whether it was
 * implemented, never whether it was complete.
 */
import { describe, expect, it } from 'vitest';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';

function walk(dir: string, out: string[] = []): string[] {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) walk(p, out);
    else if (/\.tsx?$/.test(p) && !/\.test\.tsx?$/.test(p)) out.push(p);
  }
  return out;
}

/**
 * Comments are stripped before every scan. This file documents its matchers by
 * QUOTING the defect text, and currency.ts and MoneyInput.tsx both explain the
 * `type="number"` shape they replace -- a sweep that reads comments reports the
 * documentation as the defect, which is the fastest way to get a gate disabled.
 */
function stripComments(s: string): string {
  return s.replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|[^:])\/\/[^\n]*/g, '$1');
}

const FILES = walk('src');
const read = (f: string) => stripComments(readFileSync(f, 'utf8'));
const UI = FILES.filter((f) => f.startsWith('src/components/'));

/**
 * Static guide prose. `calculator-extras.ts` renders worked examples into
 * article text that is pre-rendered to HTML at build time, long before any
 * browser picks a currency, and its figures are illustrative US options
 * examples rather than live quotes.
 */
const STATIC_PROSE = ['src/content/calculator-extras.ts'];

describe('one module formats money, and components do not reinvent it', () => {
  it('no component builds a currency string with a literal $', () => {
    const offenders: string[] = [];
    for (const f of UI) {
      for (const m of read(f).matchAll(/`[^`]*\$\$\{([^}]*)\}[^`]*`/g)) {
        offenders.push(`${f}: \${${m[1]}}`);
      }
    }
    expect(offenders).toEqual([]);
  });

  it('no component defines its own money() helper', () => {
    // Two copies of one expression is how this started.
    const offenders = UI.filter((f) =>
      /const\s+money\s*=\s*\(/.test(read(f)) && !read(f).includes('formatMoney'),
    );
    expect(offenders).toEqual([]);
  });

  it('every component rendering an amount imports the currency module', () => {
    // A component that groups must be subscribed, or it keeps the previous
    // locale's separators until something else happens to re-render it.
    const offenders: string[] = [];
    for (const f of UI) {
      const s = read(f);
      if (!/formatMoney|formatAmount/.test(s)) continue;
      if (!s.includes('useCurrency')) offenders.push(f);
    }
    expect(offenders).toEqual([]);
  });
});

describe('money inputs cannot be type=number', () => {
  it('MoneyInput is a text input with a decimal keypad', () => {
    const s = read('src/components/MoneyInput.tsx');
    expect(s).toContain('type="text"');
    expect(s).not.toContain('type="number"');
    expect(s).toContain('inputMode="decimal"');
    expect(readFileSync('src/components/MoneyInput.tsx', 'utf8')).toContain('WHY IT IS NOT');
  });
});

describe('the picker is reachable', () => {
  it('CurrencySelect is mounted in the site nav', () => {
    expect(read('src/components/SiteNav.tsx')).toContain('<CurrencySelect');
  });

  it('and it discloses that nothing is converted', () => {
    // These prices come from a live US chain. A symbol swap with no FX is a
    // display choice, and saying so is the difference between localising and
    // misstating.
    const raw = readFileSync('src/components/CurrencySelect.tsx', 'utf8');
    expect(raw).toMatch(/NOT converted|not.*converted/i);
  });
});

describe('the sweep can actually fail', () => {
  it('recognises each defect shape it is looking for', () => {
    const ticket = '{cost === null ? "—" : `$${cost.toFixed(2)}`}';
    expect([...ticket.matchAll(/`[^`]*\$\$\{([^}]*)\}[^`]*`/g)].map((m) => m[1])).toEqual([
      'cost.toFixed(2)',
    ]);

    const dupHelper = "const money = (v: number) => `${v < 0 ? '−' : ''}$${Math.abs(v)}`;";
    expect(/const\s+money\s*=\s*\(/.test(dupHelper)).toBe(true);
    expect(dupHelper.includes('formatMoney')).toBe(false);

    expect(stripComments('/** type="number" */ const a = 1;')).not.toContain('type="number"');
  });
});

describe('declared exemptions are real files, so the list cannot rot', () => {
  it.each(STATIC_PROSE)('%s still exists', (f) => {
    expect(() => readFileSync(f, 'utf8')).not.toThrow();
  });
});
