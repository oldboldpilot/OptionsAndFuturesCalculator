/**
 * Whether the US equity session is open, from the instant alone.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The chain chip said LIVE at 01:12 ET with every US exchange closed, because
 * its only test was "was this fetched in the last 60 seconds". A print can be a
 * second old and still be the last trade of a session that ended hours ago.
 * `chainFreshness` therefore also asks this, and every case here is a fact
 * about the NYSE calendar that a hand test on a Tuesday afternoon would never
 * reach: weekends, a Friday holiday that is observed on the Thursday, Good
 * Friday (which moves with Easter), and the 1pm early closes.
 *
 * Instants are written in UTC with their Eastern time beside them, because the
 * conversion is the thing under test and a test that does it in its own head
 * has one more place to be wrong. EDT is UTC-4, EST is UTC-5.
 */
import { describe, it, expect } from 'vitest';
import { usEquitySession, easterSunday } from './marketSession';

const at = (iso: string) => Date.parse(iso);

describe('usEquitySession: the ordinary week', () => {
  it('is open at 10:00 ET on a Tuesday', () => {
    // 2026-10-06 is a Tuesday, EDT.
    expect(usEquitySession(at('2026-10-06T14:00:00Z')).open).toBe(true);
  });

  it('opens at 09:30 ET exactly and not a second before', () => {
    expect(usEquitySession(at('2026-10-06T13:29:59Z')).open).toBe(false);
    expect(usEquitySession(at('2026-10-06T13:30:00Z')).open).toBe(true);
  });

  it('closes at 16:00 ET exactly', () => {
    expect(usEquitySession(at('2026-10-06T19:59:59Z')).open).toBe(true);
    expect(usEquitySession(at('2026-10-06T20:00:00Z')).open).toBe(false);
    expect(usEquitySession(at('2026-10-06T20:00:00Z')).reason).toBe('after-close');
  });

  it('is closed at 01:12 ET, the moment the live chip was caught lying', () => {
    const s = usEquitySession(at('2026-10-07T05:12:00Z'));
    expect(s.open).toBe(false);
    expect(s.reason).toBe('before-open');
  });

  it('is closed all weekend', () => {
    // 2026-10-10 is a Saturday, 2026-10-11 a Sunday.
    expect(usEquitySession(at('2026-10-10T15:00:00Z'))).toMatchObject({ open: false, reason: 'weekend' });
    expect(usEquitySession(at('2026-10-11T15:00:00Z'))).toMatchObject({ open: false, reason: 'weekend' });
  });

  it('follows daylight saving: 09:30 ET is 14:30 UTC in winter', () => {
    // 2026-12-02 is a Wednesday, EST.
    expect(usEquitySession(at('2026-12-02T14:29:00Z')).open).toBe(false);
    expect(usEquitySession(at('2026-12-02T14:30:00Z')).open).toBe(true);
  });
});

describe('usEquitySession: holidays', () => {
  it('is closed on Thanksgiving and on Christmas Day', () => {
    expect(usEquitySession(at('2026-11-26T16:00:00Z'))).toMatchObject({ open: false, reason: 'holiday' });
    expect(usEquitySession(at('2026-12-25T16:00:00Z'))).toMatchObject({ open: false, reason: 'holiday' });
  });

  it('observes a Saturday Independence Day on the Friday before it', () => {
    // 2026-07-04 is a Saturday, so the exchange is closed Friday 2026-07-03.
    expect(usEquitySession(at('2026-07-03T16:00:00Z'))).toMatchObject({ open: false, reason: 'holiday' });
  });

  it('observes a Sunday Christmas on the Monday after it', () => {
    // 2022-12-25 was a Sunday: closed Monday 2022-12-26.
    expect(usEquitySession(at('2022-12-26T16:00:00Z'))).toMatchObject({ open: false, reason: 'holiday' });
  });

  it('does NOT close the Friday when New Year falls on a Saturday', () => {
    // The one holiday NYSE does not observe backwards: 2022-01-01 was a
    // Saturday and Friday 2021-12-31 was an ordinary trading day.
    expect(usEquitySession(at('2021-12-31T16:00:00Z')).open).toBe(true);
  });

  it('is closed on Good Friday, which moves with Easter', () => {
    // Easter 2026 is 5 April, so Good Friday is 3 April.
    expect(usEquitySession(at('2026-04-03T16:00:00Z'))).toMatchObject({ open: false, reason: 'holiday' });
    expect(usEquitySession(at('2026-04-02T16:00:00Z')).open).toBe(true);
  });

  it('is closed for MLK Day, Presidents Day, Memorial Day, Juneteenth and Labor Day', () => {
    for (const d of ['2026-01-19', '2026-02-16', '2026-05-25', '2026-06-19', '2026-09-07']) {
      expect(usEquitySession(at(`${d}T16:00:00Z`)), d).toMatchObject({ open: false, reason: 'holiday' });
    }
  });
});

describe('usEquitySession: early closes', () => {
  it('closes at 13:00 ET the day after Thanksgiving', () => {
    // 2026-11-27: 17:59 UTC is 12:59 EST, 18:00 UTC is 13:00 EST.
    expect(usEquitySession(at('2026-11-27T17:59:00Z')).open).toBe(true);
    expect(usEquitySession(at('2026-11-27T18:00:00Z'))).toMatchObject({ open: false, reason: 'after-close' });
  });

  it('closes at 13:00 ET on Christmas Eve when it is a weekday', () => {
    // 2026-12-24 is a Thursday.
    expect(usEquitySession(at('2026-12-24T17:59:00Z')).open).toBe(true);
    expect(usEquitySession(at('2026-12-24T18:00:00Z')).open).toBe(false);
  });
});

describe('easterSunday', () => {
  it('matches the published dates', () => {
    expect(easterSunday(2024)).toEqual({ month: 3, day: 31 });
    expect(easterSunday(2025)).toEqual({ month: 4, day: 20 });
    expect(easterSunday(2026)).toEqual({ month: 4, day: 5 });
    expect(easterSunday(2027)).toEqual({ month: 3, day: 28 });
  });
});
