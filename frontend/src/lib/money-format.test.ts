/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Money formatting and money INPUT, with one named regression per defect found
 * on 2026-09-23. The defects on THIS site were:
 *
 *  1. There was no money module at all. `money()` existed TWICE, copied
 *     between StrategyMetrics and ProbabilityCurve, each hardcoding a `$` and
 *     the browser's default locale with different decimal rules.
 *  2. The ticket's debit/credit total rendered ``$${cost.toFixed(2)}`` -- and
 *     that figure is premium x contracts x 100, so a ten-lot at 7.25 read
 *     `$7250.00`.
 *  3. P&L matrix cells rendered `value.toFixed(0)`, so a five-figure outcome
 *     read `12775` with no separator anywhere, and compacted with a hardcoded
 *     English "k".
 *  4. Nothing followed a display currency, because there was none.
 *
 * The suite is adversarial: every assertion of a value has a sibling proving
 * the DEFECT shape fails the same check, because a formatter that returns the
 * same string for everything passes any one-directional test.
 */
import { beforeEach, describe, expect, it } from 'vitest';
import {
  caretPositionForDigits,
  currencySymbol,
  DEFAULT_CURRENCY,
  formatAmount,
  formatAmountCompact,
  formatMoney,
  formatMoneyCompact,
  formatMoneyInput,
  parseMoneyInput,
  separatorsFor,
  setCurrency,
} from './currency';

const digitsOf = (s: string) => s.replace(/[^0-9]/g, '');

beforeEach(() => setCurrency(DEFAULT_CURRENCY, false));

describe('defect 1: one formatter, and it groups in every currency', () => {
  const CASES: Array<[string, number]> = [
    ['USD', 1234567.89],
    ['EUR', 1234567.89],
    ['GBP', 1234567.89],
    ['INR', 1234567.89],
    ['JPY', 1234567],
    ['BRL', 1234567.89],
    ['CHF', 1234567.89],
    ['ZAR', 1234567.89],
  ];

  it.each(CASES)('%s never leaves a long unbroken digit run', (code, value) => {
    setCurrency(code, false);
    const out = formatMoney(value, 2);
    expect(/[0-9]{7,}/.test(out)).toBe(false);
  });

  it('the OLD helper shape fails that same check', () => {
    const bad = `$${(1234567.89).toFixed(2)}`;
    expect(bad).toBe('$1234567.89');
    expect(/[0-9]{7,}/.test(bad)).toBe(true);
  });

  it('uses the locale separators, not commas everywhere', () => {
    setCurrency('USD', false);
    const us = formatMoney(1234567, 0);
    setCurrency('EUR', false);
    const de = formatMoney(1234567, 0);
    setCurrency('INR', false);
    const inr = formatMoney(1234567, 0);

    expect(us).toContain(',');
    expect(de).toContain('.');
    expect(us).not.toBe(de);
    expect(inr.replace(/[^\d,]/g, '')).toBe('12,34,567');
  });

  it('gives a zero-decimal currency no minor units', () => {
    setCurrency('JPY', false);
    expect(formatMoney(1234567, 2)).not.toMatch(/[.,]\d\d\b/);
  });

  it('keeps the sign on a loss and still groups it', () => {
    setCurrency('USD', false);
    const out = formatMoney(-12775, 2);
    expect(out).toContain(',');
    expect(/[-−(]/.test(out)).toBe(true);
  });

  it('never renders NaN or Infinity to a user', () => {
    for (const bad of [NaN, Infinity, -Infinity]) {
      expect(formatMoney(bad, 2)).not.toMatch(/NaN|Infinity/);
      expect(formatAmount(bad, 0)).not.toMatch(/NaN|Infinity/);
    }
  });
});

describe('defect 2: the ticket total', () => {
  it('groups a ten-lot debit that used to read $7250.00', () => {
    setCurrency('USD', false);
    const cost = 7.25 * 10 * 100; // premium x contracts x multiplier
    expect(cost).toBe(7250);
    expect(formatMoney(cost, 2)).toBe('$7,250.00');
  });
});

describe('defect 3: P&L matrix cells', () => {
  it('groups a five-figure outcome that used to read 12775', () => {
    setCurrency('USD', false);
    expect(formatAmount(12775, 0)).toBe('12,775');
    expect((12775).toFixed(0)).toBe('12775'); // the defect, for contrast
  });

  it('compacts without a hardcoded English k', () => {
    setCurrency('USD', false);
    const out = formatAmountCompact(12775);
    expect(out).not.toBe('12.8k');
    expect(digitsOf(out).length).toBeLessThanOrEqual(3);
  });

  it('compacts differently per locale, which a literal k cannot', () => {
    setCurrency('USD', false);
    const en = formatAmountCompact(1_200_000);
    setCurrency('JPY', false);
    const ja = formatAmountCompact(1_200_000);
    expect(en).not.toBe(ja);
  });
});

describe('defect 4: the display currency is followed', () => {
  it('changes the symbol when the picker moves', () => {
    setCurrency('USD', false);
    expect(currencySymbol()).toBe('$');
    setCurrency('GBP', false);
    expect(currencySymbol()).toBe('£');
    setCurrency('EUR', false);
    expect(currencySymbol()).not.toBe('$');
  });

  it('compact money follows it too', () => {
    setCurrency('USD', false);
    const usd = formatMoneyCompact(250_000);
    setCurrency('EUR', false);
    expect(formatMoneyCompact(250_000)).not.toBe(usd);
  });
});

describe('parsing follows the ACTIVE locale', () => {
  it('de-DE "5.900" is 5900, not 5.9 -- a strike three orders out', () => {
    setCurrency('EUR', false);
    expect(parseMoneyInput('5.900')).toBe(5900);
  });

  it('en-US "5.900" is 5.9 -- the SAME string, a different number', () => {
    setCurrency('USD', false);
    expect(parseMoneyInput('5.900')).toBeCloseTo(5.9, 4);
  });

  it('handles fr-FR narrow and non-breaking space grouping', () => {
    expect(parseMoneyInput('1 234 567,89', 'fr-FR')).toBeCloseTo(1234567.89, 2);
    expect(parseMoneyInput('1 234 567,89', 'fr-FR')).toBeCloseTo(1234567.89, 2);
  });

  it('parses Arabic-Indic digits rather than deleting them', () => {
    const arabic = new Intl.NumberFormat('ar-EG').format(1234567);
    expect(/[0-9]/.test(arabic)).toBe(false);
    expect(parseMoneyInput(arabic, 'ar-EG')).toBe(1234567);
  });

  it('discovers separators instead of assuming them', () => {
    expect(separatorsFor('en-US')).toEqual({ group: ',', decimal: '.' });
    expect(separatorsFor('de-DE')).toEqual({ group: '.', decimal: ',' });
  });

  it.each(['$1,234.56', '1,234.56', '  $1,234.56 ', 'USD 1,234.56'])('%j parses', (t) => {
    setCurrency('USD', false);
    expect(parseMoneyInput(t)).toBeCloseTo(1234.56, 2);
  });

  it.each(['(1,234.56)', '-1,234.56', '−1,234.56'])('%j is negative', (t) => {
    setCurrency('USD', false);
    expect(parseMoneyInput(t)).toBeCloseTo(-1234.56, 2);
  });

  it.each(['', '   ', 'abc', '$', '-', '.'])('%j is NaN, never a fabricated 0', (t) => {
    expect(Number.isNaN(parseMoneyInput(t))).toBe(true);
  });

  it('a real zero is still zero', () => {
    expect(parseMoneyInput('0')).toBe(0);
  });
});

describe('format -> parse round trips across currencies and magnitudes', () => {
  it('returns the original number every time', () => {
    const VALUES = [0, 1, 725, 1000, 7250, 12775, 999999, 1234567, 987654321];
    for (const code of ['USD', 'EUR', 'GBP', 'INR', 'BRL', 'CHF', 'ZAR']) {
      setCurrency(code, false);
      for (const v of VALUES) {
        const back = parseMoneyInput(formatMoneyInput(v, 0));
        expect(`${code}:${v}:${back}`).toBe(`${code}:${v}:${v}`);
      }
    }
  });
});

describe('the money input carries no symbol', () => {
  it('omits it, because the field renders it separately', () => {
    setCurrency('USD', false);
    expect(formatMoneyInput(475000, 0)).toBe('475,000');
    expect(formatMoneyInput(475000, 0)).not.toContain('$');
  });

  it('is blank for a non-finite value so the field can be empty', () => {
    expect(formatMoneyInput(NaN, 0)).toBe('');
  });
});

describe('caret position survives regrouping', () => {
  it('follows the digit, not the character index', () => {
    expect(caretPositionForDigits('1,000', 4)).toBe(5);
    expect(caretPositionForDigits('999', 3)).toBe(3);
    expect(caretPositionForDigits('475,000', 3)).toBe(3);
    expect(caretPositionForDigits('475,000', 4)).toBe(5);
  });

  it('stays at the start with no digits before it', () => {
    expect(caretPositionForDigits('475,000', 0)).toBe(0);
  });

  it('clamps rather than returning a bogus index', () => {
    expect(caretPositionForDigits('475,000', 99)).toBe(7);
  });
});
