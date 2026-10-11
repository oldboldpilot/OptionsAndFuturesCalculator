/**
 * What an advertising slot should be doing, given what Google has told it.
 *
 * Two empty boxes labelled "SPONSORED", about 300px tall each, sat between the
 * end of every guide and the footer. The slot reserves its height before the
 * ad script runs (a late reflow under a visitor's cursor is its own bug), and
 * then never gave the space back when nothing arrived: AdSense marks an empty
 * unit `data-ad-status="unfilled"`, and an ad-blocked or failed load marks
 * nothing at all. A reservation is a promise that something is coming; once it
 * is clear nothing is, the box has to go.
 *
 * Pure over three values so the rule can be tested without a browser or an ad
 * network. The component supplies the status it observed and how long it has
 * waited.
 *
 *   reserve   keep the space: an ad may still arrive
 *   show      an ad filled the slot
 *   collapse  nothing is coming: take the space back, label and all
 *
 * This decides only whether to RENDER an already-allowed slot. Where ads may
 * appear at all is `config/ad-routes.ts`, and nothing here widens that.
 */
export type AdSlotVerdict = 'reserve' | 'show' | 'collapse';

/** How long a slot waits for a status before concluding the script never ran. */
export const AD_FILL_TIMEOUT_MS = 4000;

export function resolveAdSlot(
  status: string | null,
  waitedMs: number,
  timeoutMs: number = AD_FILL_TIMEOUT_MS,
): AdSlotVerdict {
  if (status === 'filled') return 'show';
  if (status === 'unfilled') return 'collapse';
  return waitedMs >= timeoutMs ? 'collapse' : 'reserve';
}
