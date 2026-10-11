'use client';

import { useEffect, useId, useRef, useState } from 'react';

/**
 * An explanation a visitor can actually get at.
 *
 * Most of the explanations on this screen -- where the risk-free rate came from,
 * what "no div" means, why a strategy needs Pro, what the matrix price bounds
 * do to the headline numbers -- were `title` attributes: 2,446 of them on the
 * tool page. A `title` appears only on mouse hover, so on a phone it does not
 * exist, on a keyboard it is reached only by focusing the exact element, and a
 * screen reader may or may not read it. The ones that carry information a
 * reader needs belong in a control anyone can operate.
 *
 * A button, so it takes focus and works under a finger; it toggles a short note
 * rather than opening a modal, because the note is a sentence or two; Escape
 * and a press anywhere else close it.
 */
export function InfoTip({
  label,
  children,
  align = 'start',
}: {
  /** What the button is for, e.g. "About the risk-free rate". */
  label: string;
  children: React.ReactNode;
  /** Which edge of the button the note lines up with. Use `end` near a right-hand edge. */
  align?: 'start' | 'end';
}) {
  const [open, setOpen] = useState(false);
  const id = useId();
  const root = useRef<HTMLSpanElement | null>(null);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setOpen(false);
    };
    const onDown = (e: PointerEvent) => {
      if (root.current && !root.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('keydown', onKey);
    document.addEventListener('pointerdown', onDown);
    return () => {
      document.removeEventListener('keydown', onKey);
      document.removeEventListener('pointerdown', onDown);
    };
  }, [open]);

  return (
    <span className="infotip" ref={root}>
      <button
        type="button"
        className="infotip-btn"
        aria-label={label}
        aria-expanded={open}
        aria-controls={open ? id : undefined}
        onClick={() => setOpen((v) => !v)}
      >
        i
      </button>
      {open && (
        <span id={id} role="note" className="infotip-body" data-align={align}>
          {children}
        </span>
      )}
    </span>
  );
}

export default InfoTip;
