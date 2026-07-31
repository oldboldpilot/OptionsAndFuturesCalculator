'use client';

/**
 * Reserved advertising space.
 *
 * The slot holds its own height from first paint, before any ad script runs.
 * An ad unit that sizes itself on arrival shoves the page down under the
 * reader's cursor — on a page where the thing below is a strike ladder with
 * buy and sell buttons, a late reflow is a misclick that opens a position.
 * Reserving the box is the difference between an ad and an accident.
 *
 * No ad script is loaded here yet: ads.txt authorises the seller, but a unit
 * also needs its own slot id. Until that exists the space stays empty and
 * labelled rather than filled with a placeholder pretending to be content.
 */
export function AdSlot({
  size = 'leaderboard',
  label = 'Advertisement',
}: {
  size?: 'leaderboard' | 'rectangle';
  label?: string;
}) {
  // IAB standard units, so the reserved box matches what will occupy it.
  const dims = size === 'leaderboard'
    ? { width: 728, height: 90 }
    : { width: 300, height: 250 };

  return (
    <aside
      aria-label={label}
      style={{
        width: '100%',
        maxWidth: dims.width,
        height: dims.height,
        margin: '0 auto',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        border: '1px dashed var(--color-line)',
        borderRadius: 'var(--radius-sm)',
        background: 'var(--color-base-700)',
        color: 'var(--color-ink-400)',
        fontSize: 'var(--text-2xs)',
        letterSpacing: '0.08em',
        textTransform: 'uppercase',
        flex: 'none',
      }}
    >
      {label}
    </aside>
  );
}

export default AdSlot;
