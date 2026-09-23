/**
 * Display currency and locale.
 *
 * FORMATTING ONLY — NOTHING HERE CONVERTS. The amount is unchanged; only the
 * symbol, the grouping separator and the decimal style move. That is a
 * deliberate limit and it is stated in the picker's own tooltip, because this
 * site is not a mortgage calculator where the user types the numbers: an option
 * premium comes from a live US chain and is denominated in dollars. Showing
 * 7,250 with a euro sign is the same class of defect as the LIVE badge derived
 * from request status — fabrication by labelling — so the UI says what it is
 * doing rather than letting the symbol imply a conversion that did not happen.
 *
 * Kept deliberately parallel to mortgage-nest-egg's `src/lib/currency.ts`: two
 * repositories, one engine, and a user who may well visit both. The currency
 * list, the country map and `formatMoney`'s signature are the same on purpose,
 * so a divergence is visible as a diff rather than hidden behind two designs.
 * The storage key is NOT shared (`ofc.currency` vs `mfvc.currency`) — the sites
 * are different origins, so localStorage cannot be shared anyway, and giving
 * them the same key would suggest otherwise.
 */
import { useSyncExternalStore } from 'react';

export interface CurrencyDef {
  code: string;
  label: string;
  locale: string;
}

export const CURRENCIES: CurrencyDef[] = [
  { code: 'USD', label: 'US Dollar', locale: 'en-US' },
  { code: 'EUR', label: 'Euro', locale: 'de-DE' },
  { code: 'GBP', label: 'British Pound', locale: 'en-GB' },
  { code: 'CAD', label: 'Canadian Dollar', locale: 'en-CA' },
  { code: 'AUD', label: 'Australian Dollar', locale: 'en-AU' },
  { code: 'NZD', label: 'New Zealand Dollar', locale: 'en-NZ' },
  { code: 'CHF', label: 'Swiss Franc', locale: 'de-CH' },
  { code: 'SEK', label: 'Swedish Krona', locale: 'sv-SE' },
  { code: 'NOK', label: 'Norwegian Krone', locale: 'nb-NO' },
  { code: 'DKK', label: 'Danish Krone', locale: 'da-DK' },
  { code: 'PLN', label: 'Polish Zloty', locale: 'pl-PL' },
  { code: 'CZK', label: 'Czech Koruna', locale: 'cs-CZ' },
  { code: 'JPY', label: 'Japanese Yen', locale: 'ja-JP' },
  { code: 'CNY', label: 'Chinese Yuan', locale: 'zh-CN' },
  { code: 'HKD', label: 'Hong Kong Dollar', locale: 'en-HK' },
  { code: 'SGD', label: 'Singapore Dollar', locale: 'en-SG' },
  { code: 'INR', label: 'Indian Rupee', locale: 'en-IN' },
  { code: 'AED', label: 'UAE Dirham', locale: 'en-AE' },
  { code: 'SAR', label: 'Saudi Riyal', locale: 'en-SA' },
  { code: 'ZAR', label: 'South African Rand', locale: 'en-ZA' },
  { code: 'NGN', label: 'Nigerian Naira', locale: 'en-NG' },
  { code: 'KES', label: 'Kenyan Shilling', locale: 'en-KE' },
  { code: 'GHS', label: 'Ghanaian Cedi', locale: 'en-GH' },
  { code: 'BRL', label: 'Brazilian Real', locale: 'pt-BR' },
  { code: 'MXN', label: 'Mexican Peso', locale: 'es-MX' },
  { code: 'ARS', label: 'Argentine Peso', locale: 'es-AR' },
  { code: 'CLP', label: 'Chilean Peso', locale: 'es-CL' },
  { code: 'COP', label: 'Colombian Peso', locale: 'es-CO' },
  { code: 'TRY', label: 'Turkish Lira', locale: 'tr-TR' },
  { code: 'ILS', label: 'Israeli Shekel', locale: 'he-IL' },
  { code: 'KRW', label: 'South Korean Won', locale: 'ko-KR' },
  { code: 'MYR', label: 'Malaysian Ringgit', locale: 'ms-MY' },
  { code: 'IDR', label: 'Indonesian Rupiah', locale: 'id-ID' },
  { code: 'THB', label: 'Thai Baht', locale: 'th-TH' },
  { code: 'PHP', label: 'Philippine Peso', locale: 'en-PH' },
  { code: 'VND', label: 'Vietnamese Dong', locale: 'vi-VN' },
  { code: 'PKR', label: 'Pakistani Rupee', locale: 'en-PK' },
  { code: 'EGP', label: 'Egyptian Pound', locale: 'ar-EG' },
];

const EURO = [
  'AT', 'BE', 'CY', 'DE', 'EE', 'ES', 'FI', 'FR', 'GR', 'HR', 'IE', 'IT',
  'LT', 'LU', 'LV', 'MT', 'NL', 'PT', 'SI', 'SK', 'AD', 'MC', 'SM', 'VA',
  'ME', 'XK',
];

export const COUNTRY_CURRENCY: Record<string, string> = {
  US: 'USD', PR: 'USD', EC: 'USD', SV: 'USD',
  GB: 'GBP', GI: 'GBP', IM: 'GBP', JE: 'GBP', GG: 'GBP',
  CA: 'CAD', AU: 'AUD', NZ: 'NZD', CH: 'CHF', LI: 'CHF',
  SE: 'SEK', NO: 'NOK', DK: 'DKK', PL: 'PLN', CZ: 'CZK',
  JP: 'JPY', CN: 'CNY', HK: 'HKD', SG: 'SGD', IN: 'INR',
  AE: 'AED', SA: 'SAR', ZA: 'ZAR', NG: 'NGN', KE: 'KES', GH: 'GHS',
  BR: 'BRL', MX: 'MXN', AR: 'ARS', CL: 'CLP', CO: 'COP',
  TR: 'TRY', IL: 'ILS', KR: 'KRW', MY: 'MYR', ID: 'IDR',
  TH: 'THB', PH: 'PHP', VN: 'VND', PK: 'PKR', EG: 'EGP',
  ...Object.fromEntries(EURO.map((c) => [c, 'EUR'])),
};

export const DEFAULT_CURRENCY = 'USD';
const STORAGE_KEY = 'ofc.currency';

function def(code: string): CurrencyDef {
  return CURRENCIES.find((c) => c.code === code) ?? CURRENCIES[0];
}

let current: CurrencyDef = def(DEFAULT_CURRENCY);
let detected = false;
const listeners = new Set<() => void>();

function emit() {
  for (const l of listeners) l();
}

export function getCurrency(): CurrencyDef {
  return current;
}

export function setCurrency(code: string, persist = true) {
  const next = def(code);
  if (next.code === current.code) return;
  current = next;
  if (persist && typeof window !== 'undefined') {
    try {
      window.localStorage.setItem(STORAGE_KEY, next.code);
    } catch {
      /* private mode, blocked storage — formatting must not depend on it */
    }
  }
  emit();
}

export function currencyForCountry(country?: string | null): string {
  if (!country) return DEFAULT_CURRENCY;
  return COUNTRY_CURRENCY[country.toUpperCase()] ?? DEFAULT_CURRENCY;
}

function countryFromNavigator(): string | null {
  if (typeof navigator === 'undefined') return null;
  const langs = [navigator.language, ...(navigator.languages ?? [])];
  for (const l of langs) {
    const region = l?.split('-')[1];
    if (region && region.length === 2) return region;
  }
  return null;
}

/** Detect the visitor's currency once per session (client only). */
export async function detectCurrency() {
  if (detected || typeof window === 'undefined') return;
  detected = true;

  try {
    const saved = window.localStorage.getItem(STORAGE_KEY);
    if (saved) {
      setCurrency(saved, false);
      return;
    }
  } catch {
    /* ignore */
  }

  const local = countryFromNavigator();
  if (local) setCurrency(currencyForCountry(local), false);

  // Cloudflare edge geo — more accurate than browser locale. Only exists on
  // the Cloudflare edge, so it is skipped in dev to avoid a noisy 404.
  if (process.env.NODE_ENV !== 'production') return;
  try {
    const res = await fetch('/cdn-cgi/trace', { cache: 'no-store' });
    if (res.ok) {
      const text = await res.text();
      const loc = /(?:^|\n)loc=([A-Z]{2})/.exec(text)?.[1];
      if (loc) setCurrency(currencyForCountry(loc), false);
    }
  } catch {
    /* ignore — the locale fallback above already applied */
  }
}

/**
 * THE one money formatter for this site. Everything a user reads as an amount
 * goes through here, and `money-format.test.ts` sweeps the source to prove it.
 *
 * Grouping is the reason this exists: `toFixed` renders 7250 as "7250.00",
 * which is unreadable at a glance and wrong in every locale. `Intl` groups it
 * AND picks the right separator — 7,250.00 in en-US, 7.250,00 in de-DE,
 * 7 250,00 in fr-FR, and 12,34,568 rather than 1,234,568 in en-IN, which no
 * hand-rolled thousands regex gets right.
 *
 * `digits` is the MAXIMUM and also the minimum, so a column of amounts lines
 * up instead of ragging on whether a value happened to be round. The exception
 * is a zero-decimal currency: JPY, KRW, VND and CLP have no minor unit, and
 * asking Intl for 2 decimals there prints ¥7,250.00, which is not a thing.
 * `resolvedOptions()` is asked rather than a hardcoded list, so the set stays
 * correct as ICU data changes.
 */
export function formatMoney(n: number, digits = 2): string {
  const c = current;
  const value = Number.isFinite(n) ? n : 0;
  try {
    // `maximumFractionDigits` is optional in the TS lib types even though ICU
    // always resolves one; falling back to `digits` keeps the caller's intent
    // rather than silently dropping to zero decimals.
    const natural =
      new Intl.NumberFormat(c.locale, {
        style: 'currency',
        currency: c.code,
      }).resolvedOptions().maximumFractionDigits ?? digits;
    const dp = Math.min(digits, natural);
    return new Intl.NumberFormat(c.locale, {
      style: 'currency',
      currency: c.code,
      minimumFractionDigits: dp,
      maximumFractionDigits: dp,
    }).format(value);
  } catch {
    // An unknown currency code or a locale ICU does not carry. Still GROUPED —
    // the fallback must not be the defect this module exists to remove.
    try {
      return `${c.code} ${value.toLocaleString(undefined, {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
      })}`;
    } catch {
      return `${c.code} ${value.toFixed(digits)}`;
    }
  }
}

/**
 * A bare grouped number, for axis ticks and anywhere a symbol would be noise
 * because the column is already labelled as money.
 */
export function formatAmount(n: number, digits = 0): string {
  const value = Number.isFinite(n) ? n : 0;
  try {
    return new Intl.NumberFormat(current.locale, {
      minimumFractionDigits: digits,
      maximumFractionDigits: digits,
    }).format(value);
  } catch {
    return value.toFixed(digits);
  }
}

/**
 * Compact money for a dense grid, where a full amount would not fit: 12.8k,
 * 1.2M. Still locale-aware — `notation: 'compact'` picks the locale's own
 * abbreviation, which is not "k" everywhere.
 */
export function formatMoneyCompact(n: number): string {
  const c = current;
  const value = Number.isFinite(n) ? n : 0;
  try {
    return new Intl.NumberFormat(c.locale, {
      style: 'currency',
      currency: c.code,
      notation: 'compact',
      maximumFractionDigits: 1,
    }).format(value);
  } catch {
    return formatMoney(value, 0);
  }
}

/**
 * The locale's own grouping and decimal separators, discovered from Intl
 * rather than assumed.
 *
 * Assuming "," groups and "." decimates is wrong for most of the world and
 * catastrophic rather than cosmetic on INPUT: in de-DE "475.000" means four
 * hundred and seventy-five thousand, and parsing it as a decimal gives 475.
 * fr-FR groups with U+202F (narrow no-break space) and older ICU used U+00A0,
 * neither of which is the space on anyone's keyboard.
 */
export function separatorsFor(locale: string): { group: string; decimal: string } {
  try {
    const parts = new Intl.NumberFormat(locale).formatToParts(12345.6);
    return {
      group: parts.find((p) => p.type === 'group')?.value ?? ',',
      decimal: parts.find((p) => p.type === 'decimal')?.value ?? '.',
    };
  } catch {
    return { group: ',', decimal: '.' };
  }
}

/** The symbol for the active currency, for use as an input prefix. */
export function currencySymbol(): string {
  const c = current;
  try {
    const parts = new Intl.NumberFormat(c.locale, {
      style: 'currency',
      currency: c.code,
    }).formatToParts(0);
    return parts.find((p) => p.type === 'currency')?.value ?? c.code;
  } catch {
    return c.code;
  }
}

/**
 * Map a locale's own digits onto ASCII.
 *
 * ar-EG formats 1234 as \u0661\u066c\u0662\u0663\u0664 with ARABIC-INDIC digits, and hi-IN, bn-IN
 * and fa-IR do the same with their own. A parser keeping only `[0-9]` deletes
 * every digit in those locales and reports NaN for a valid amount. The digit
 * set is DISCOVERED by formatting a known number, so this stays right for
 * numbering systems nobody here has thought about. ASCII is always accepted
 * too, because a keyboard may produce it regardless of locale.
 */
function toAsciiDigits(text: string, locale: string): string {
  try {
    const localeDigits = new Intl.NumberFormat(locale, { useGrouping: false }).format(9876543210);
    if (localeDigits === '9876543210') return text;
    const map = new Map<string, string>();
    const ascii = '9876543210';
    for (let i = 0; i < localeDigits.length && i < ascii.length; i++) {
      map.set(localeDigits[i], ascii[i]);
    }
    let out = '';
    for (const ch of text) out += map.get(ch) ?? ch;
    return out;
  } catch {
    return text;
  }
}

/**
 * Parse what a human typed into a money field, in the ACTIVE locale.
 *
 * Permissive about decoration -- a person pasting from a statement brings the
 * symbol and the spaces with them -- and strict about separators, which is the
 * one ambiguity where guessing changes the NUMBER rather than its presentation.
 *
 * Returns NaN when there is no digit at all, so a caller can tell "empty" from
 * a real zero. Callers must not coerce that to 0: an empty field is not a
 * zero-dollar premium.
 */
export function parseMoneyInput(text: string, locale = current.locale): number {
  if (typeof text !== 'string') return NaN;
  const { group, decimal } = separatorsFor(locale);

  let t = toAsciiDigits(text.trim(), locale);
  if (!t) return NaN;

  let negative = false;
  if (/^\((.*)\)$/.test(t)) {
    negative = true;
    t = t.replace(/^\((.*)\)$/, '$1');
  }
  if (/^[-\u2212]/.test(t) || /[-\u2212]$/.test(t)) negative = true;

  t = t.replace(/[\s\u00a0\u202f\u2009]/g, '');
  if (group) t = t.split(group).join('');
  if (decimal && decimal !== '.') t = t.split(decimal).join('.');
  t = t.replace(/[^0-9.]/g, '');

  if (!/[0-9]/.test(t)) return NaN;
  const bits = t.split('.');
  if (bits.length > 2) t = bits.slice(0, -1).join('') + '.' + bits[bits.length - 1];

  const n = Number(t);
  if (!Number.isFinite(n)) return NaN;
  return negative ? -n : n;
}

/**
 * The grouped text a money INPUT shows. No currency symbol: the field renders
 * that as a prefix outside the input, so baking it in would double it and be
 * re-parsed on the next keystroke.
 */
export function formatMoneyInput(n: number, digits = 0): string {
  if (!Number.isFinite(n)) return '';
  try {
    return new Intl.NumberFormat(current.locale, {
      minimumFractionDigits: 0,
      maximumFractionDigits: digits,
    }).format(n);
  } catch {
    return String(n);
  }
}

/**
 * Where the caret belongs after a money field regroups itself.
 *
 * Counting DIGITS rather than characters is what makes it survive a separator
 * appearing or vanishing -- exactly what happens on the keystroke that takes
 * 999 to 1,000, where the character index shifts by one and the digit index
 * does not. Pure and exported so it is testable without a DOM.
 */
export function caretPositionForDigits(formatted: string, digitsBefore: number): number {
  if (digitsBefore <= 0) return 0;
  let seen = 0;
  for (let i = 0; i < formatted.length; i++) {
    if (/[0-9]/.test(formatted[i])) {
      seen++;
      if (seen === digitsBefore) return i + 1;
    }
  }
  return formatted.length;
}

/**
 * A bare compact NUMBER (no symbol), for a grid cell too narrow for the full
 * amount. `notation: 'compact'` picks the locale's own abbreviation -- the
 * hardcoded "k" it replaces was English in every locale, and ja-JP counts in
 * 万 rather than thousands.
 */
export function formatAmountCompact(n: number): string {
  const value = Number.isFinite(n) ? n : 0;
  try {
    return new Intl.NumberFormat(current.locale, {
      notation: 'compact',
      maximumFractionDigits: 1,
    }).format(value);
  } catch {
    return formatAmount(value, 0);
  }
}

function subscribe(cb: () => void) {
  listeners.add(cb);
  return () => listeners.delete(cb);
}

/** Subscribe a component to currency changes so money re-renders. */
export function useCurrency(): CurrencyDef {
  return useSyncExternalStore(
    subscribe,
    () => current,
    () => def(DEFAULT_CURRENCY),
  );
}

/** Test-only: restore module state between cases. */
export function __resetCurrencyForTests() {
  current = def(DEFAULT_CURRENCY);
  detected = false;
  listeners.clear();
}
