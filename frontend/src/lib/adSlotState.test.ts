/**
 * An unfilled ad slot gives its space back (OFC-028).
 *
 * Observed: two empty "SPONSORED" boxes, roughly 300px each, between the end of
 * a guide and the footer -- reserved space with no ad in it, reading as a bug.
 */
import { describe, it, expect } from 'vitest';
import { resolveAdSlot, AD_FILL_TIMEOUT_MS } from './adSlotState';

describe('resolveAdSlot', () => {
  it('keeps the reservation while an ad may still arrive', () => {
    expect(resolveAdSlot(null, 0)).toBe('reserve');
    expect(resolveAdSlot(null, AD_FILL_TIMEOUT_MS - 1)).toBe('reserve');
  });

  it('shows a filled slot, however long it took', () => {
    expect(resolveAdSlot('filled', 0)).toBe('show');
    expect(resolveAdSlot('filled', AD_FILL_TIMEOUT_MS * 10)).toBe('show');
  });

  it('collapses the moment Google says the unit is unfilled', () => {
    expect(resolveAdSlot('unfilled', 0)).toBe('collapse');
  });

  it('collapses when no status ever arrives (blocked, offline, script failed)', () => {
    expect(resolveAdSlot(null, AD_FILL_TIMEOUT_MS)).toBe('collapse');
  });

  it('does not mistake an unrecognised status for a fill', () => {
    // A value this code was not written for must not show a box it cannot vouch for.
    expect(resolveAdSlot('weird', 0)).toBe('reserve');
    expect(resolveAdSlot('weird', AD_FILL_TIMEOUT_MS)).toBe('collapse');
  });
});
