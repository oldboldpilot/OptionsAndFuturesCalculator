'use client';

import { useCallback, useSyncExternalStore } from 'react';

type Theme = 'slate' | 'light';

const KEY = 'ofc-theme';
const EVENT = 'ofc-theme-change';

/**
 * Theme switch.
 *
 * Every surface, border and text colour in globals.css is a custom property,
 * so switching themes is one attribute on <html> — no component knows or
 * cares which is active. The `data-theme` attribute is the single source of
 * truth (set before first paint by the inline script in layout.tsx), and this
 * component subscribes to it rather than keeping a second copy in React
 * state that could drift out of sync with what is actually rendered.
 */
function subscribe(onChange: () => void) {
  window.addEventListener(EVENT, onChange);
  window.addEventListener('storage', onChange);
  return () => {
    window.removeEventListener(EVENT, onChange);
    window.removeEventListener('storage', onChange);
  };
}

function getSnapshot(): Theme {
  return (document.documentElement.dataset.theme as Theme | undefined) ?? 'light';
}

/** Prerendered HTML has no localStorage, so it always renders the default. */
function getServerSnapshot(): Theme {
  return 'light';
}

export function ThemeToggle() {
  const theme = useSyncExternalStore(subscribe, getSnapshot, getServerSnapshot);

  const pick = useCallback((next: Theme) => {
    document.documentElement.dataset.theme = next;
    try {
      localStorage.setItem(KEY, next);
    } catch {
      // Private browsing or blocked storage: the theme still applies for this
      // session, it just will not survive a reload. Not worth surfacing.
    }
    window.dispatchEvent(new Event(EVENT));
  }, []);

  return (
    <div className="segment" role="group" aria-label="Colour theme">
      <button
        className="segment-item"
        data-active={theme === 'slate'}
        onClick={() => pick('slate')}
        title="Slate theme"
      >
        Slate
      </button>
      <button
        className="segment-item"
        data-active={theme === 'light'}
        onClick={() => pick('light')}
        title="Light theme"
      >
        Light
      </button>
    </div>
  );
}

export default ThemeToggle;
