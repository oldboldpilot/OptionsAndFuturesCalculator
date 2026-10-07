/**
 * The LIVE/DELAYED rule for the option-chain chip.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The chain cache TTL is 15 minutes. The chip is the only thing on the page
 * that tells a trader whether the quotes they are about to price against are
 * current, so every case here is about the label not overstating the data.
 *
 * The two cases that motivated extracting this out of the component are the
 * skew case and the elapsed case: both render LIVE over stale quotes, both
 * pass a hand test on a machine whose clock happens to agree with the server,
 * and neither is visible in a screenshot.
 */
import { describe, it, expect } from 'vitest';
import {
  chainFreshness,
  LIVE_MAX_AGE_SECONDS,
  CLOCK_SKEW_TOLERANCE_SECONDS,
} from './chainFreshness';

// 11:00 ET on a Tuesday, mid-session. This was 21:00Z -- 17:00 ET, an hour
// AFTER the close -- which was harmless while the rule was only "under a minute
// old", and became wrong the moment LIVE also had to mean "the market is open":
// every case below that expects LIVE needs a session that is actually open.
const SERVER_PRINT = '2026-08-11T15:00:00Z';
const printMs = Date.parse(SERVER_PRINT);

describe('chainFreshness', () => {
  it('calls a just-fetched print LIVE', () => {
    const f = chainFreshness(SERVER_PRINT, printMs + 1_000);
    expect(f.isLive).toBe(true);
    expect(f.ageSeconds).toBe(1);
  });

  it('is DELAYED the moment the live window closes', () => {
    // Exactly at the boundary is NOT live: the window is the span in which the
    // claim is defensible, and the boundary is where it stops being so.
    expect(chainFreshness(SERVER_PRINT, printMs + LIVE_MAX_AGE_SECONDS * 1_000).isLive).toBe(false);
    expect(chainFreshness(SERVER_PRINT, printMs + (LIVE_MAX_AGE_SECONDS - 1) * 1_000).isLive).toBe(true);
  });

  it('is DELAYED at a full cache TTL of age, and reports that age', () => {
    const f = chainFreshness(SERVER_PRINT, printMs + 900 * 1_000);
    expect(f.isLive).toBe(false);
    expect(f.ageSeconds).toBe(900);
    expect(f.asOfTime).not.toBe('');
  });

  it('does NOT call a stale print LIVE when the viewer clock runs behind the server', () => {
    // The defect this pins: age = now - fetched goes negative on a slow client
    // clock, and `age < 60` reads negative as fresh. Here the browser is 20
    // minutes behind while the print is a full TTL old.
    const staleAt = printMs + 900 * 1_000;
    const browserBehindBy20Min = staleAt - 20 * 60 * 1_000;
    const f = chainFreshness(SERVER_PRINT, browserBehindBy20Min);
    expect(f.isLive).toBe(false);
    // The age is unknowable from this clock, so it is withheld rather than
    // reported as a negative number.
    expect(f.ageSeconds).toBeNull();
    // The server's own timestamp survives — only the derived claim is dropped.
    expect(f.asOfTime).not.toBe('');
  });

  it('absorbs benign sub-tolerance skew instead of flapping to DELAYED', () => {
    const f = chainFreshness(SERVER_PRINT, printMs - (CLOCK_SKEW_TOLERANCE_SECONDS - 1) * 1_000);
    expect(f.isLive).toBe(true);
  });

  it('is DELAYED when there is no timestamp at all', () => {
    for (const missing of [null, undefined, '']) {
      const f = chainFreshness(missing, printMs);
      expect(f.isLive).toBe(false);
      expect(f.ageSeconds).toBeNull();
      expect(f.asOfTime).toBe('');
    }
  });

  it('is DELAYED when the timestamp does not parse', () => {
    const f = chainFreshness('not-a-timestamp', printMs);
    expect(f.isLive).toBe(false);
    expect(f.ageSeconds).toBeNull();
  });

  it('goes stale as time passes without the print changing', () => {
    // What the component's ticker is for: the same fetched_at must stop being
    // LIVE on its own once enough wall clock has elapsed.
    expect(chainFreshness(SERVER_PRINT, printMs + 10 * 1_000).isLive).toBe(true);
    expect(chainFreshness(SERVER_PRINT, printMs + 120 * 1_000).isLive).toBe(false);
  });
});

describe('chainFreshness: the market has to be open for LIVE to mean anything', () => {
  // A print can be one second old and still be the last trade of a session that
  // ended hours ago. Observed live at 01:12 ET: LIVE, over a closed market.
  const AFTER_HOURS_PRINT = '2026-10-07T05:12:00Z'; // 01:12 EDT Wednesday
  const afterMs = Date.parse(AFTER_HOURS_PRINT);

  it('is NOT live when the print is a second old but the exchange is closed', () => {
    const f = chainFreshness(AFTER_HOURS_PRINT, afterMs + 1_000);
    expect(f.isLive).toBe(false);
    expect(f.state).toBe('closed');
  });

  it('says when the last quote was, on the exchange clock', () => {
    // 15:59:40 ET, 20 seconds before the close, read at 16:00:30.
    const lastTrade = '2026-10-06T19:59:40Z';
    const f = chainFreshness(lastTrade, Date.parse('2026-10-06T20:00:30Z'));
    expect(f.state).toBe('closed');
    expect(f.lastQuoteLabel).toBe('3:59 PM ET');
  });

  it('is live only when BOTH fresh and open', () => {
    expect(chainFreshness(SERVER_PRINT, printMs + 5_000).state).toBe('live');
    // Open but old: delayed, not closed -- the cache is stale, the market is not.
    expect(chainFreshness(SERVER_PRINT, printMs + 400_000).state).toBe('delayed');
  });

  it('is unknown, not closed, when the timestamp cannot be read', () => {
    expect(chainFreshness(null, printMs).state).toBe('unknown');
    expect(chainFreshness('garbage', printMs).state).toBe('unknown');
  });

  it('stays closed over a weekend however fresh the print', () => {
    const saturday = '2026-10-10T15:00:00Z';
    expect(chainFreshness(saturday, Date.parse(saturday) + 2_000).state).toBe('closed');
  });
});
