'use client';

import { useRef } from 'react';
import {
  caretPositionForDigits,
  currencySymbol,
  formatMoneyInput,
  parseMoneyInput,
  useCurrency,
} from '../lib/currency';

/**
 * THE money input. One implementation, used by every amount field on the site.
 *
 * WHY IT IS NOT `type="number"`, because that is the first thing anyone will
 * try to change back: the HTML spec restricts a number input's value to a
 * "valid floating-point number", which has no grouping separator in it. Every
 * browser therefore refuses or strips a comma, so `475,000` is simply not
 * representable there. No amount of formatting makes the number input group;
 * the element has to change. `inputMode="decimal"` keeps the numeric keypad on
 * a phone, which is what the number type was really buying.
 *
 * Grouping matters more on input than on output: `7250` and `72500` differ by
 * one keystroke and look alike, while `7,250` and `72,500` do not. On this site
 * that is a premium multiplied by a contract multiplier of 100, so a slipped
 * digit is a position priced an order of magnitude wrong.
 */
export function MoneyInput({
  value,
  onChange,
  decimals = 2,
  ariaLabel,
  className = '',
  id,
  placeholder,
}: {
  value: number;
  onChange: (v: number) => void;
  decimals?: number;
  ariaLabel?: string;
  className?: string;
  id?: string;
  placeholder?: string;
}) {
  useCurrency();
  const ref = useRef<HTMLInputElement>(null);

  const onInput = (e: React.ChangeEvent<HTMLInputElement>) => {
    const el = e.target;
    const raw = el.value;
    const caret = el.selectionStart ?? raw.length;
    const digitsBefore = raw.slice(0, caret).replace(/[^0-9]/g, '').length;

    const parsed = parseMoneyInput(raw);
    if (Number.isNaN(parsed)) {
      // An empty field is NOT zero. Reporting 0 would price a position nobody
      // described, so NaN travels and the caller decides.
      if (raw.trim() === '') onChange(NaN);
      return;
    }
    onChange(parsed);

    const next = formatMoneyInput(parsed, decimals);
    requestAnimationFrame(() => {
      const node = ref.current;
      if (!node) return;
      const pos = caretPositionForDigits(next, digitsBefore);
      try {
        node.setSelectionRange(pos, pos);
      } catch {
        /* detached or unsupported; the value is still correct */
      }
    });
  };

  return (
    <span className="money-input">
      <span className="money-input-symbol">{currencySymbol()}</span>
      <input
        ref={ref}
        id={id}
        aria-label={ariaLabel}
        type="text"
        inputMode="decimal"
        autoComplete="off"
        placeholder={placeholder}
        value={Number.isFinite(value) ? formatMoneyInput(value, decimals) : ''}
        onChange={onInput}
        className={className}
      />
    </span>
  );
}
