/**
 * Whether the US equity session is open, derived from the instant alone.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * The option-chain chip used to call a chain LIVE whenever it was under a
 * minute old. That measures how recently WE asked the provider, not whether
 * anything is trading: at 01:12 ET, with every US exchange closed, a chain
 * fetched a second ago is the last trade of the previous session and the chip
 * said LIVE. So the chip also asks this.
 *
 * It is a pure function of `nowMs` so a test can state the instant. The
 * calendar is derived by rule rather than listed, because a hand-typed list of
 * holiday dates is the thing that is correct on the day it is written and
 * silently wrong every January after it; a rule is wrong only if the exchange
 * changes the rule.
 *
 * What it does NOT model, stated rather than implied: a trading halt, a
 * one-off closure (a state funeral, a storm), pre-market and after-hours
 * trading (the chain is regular-session options), and futures, whose hours are
 * different and which this chip does not label. Where it is wrong it is wrong
 * toward "closed", which understates the data rather than overstating it.
 */

export type ClosedReason = 'weekend' | 'holiday' | 'before-open' | 'after-close';

export interface EquitySession {
  open: boolean;
  /** Present exactly when `open` is false. */
  reason?: ClosedReason;
}

const OPEN_MINUTES = 9 * 60 + 30;
const CLOSE_MINUTES = 16 * 60;
const EARLY_CLOSE_MINUTES = 13 * 60;

const eastern = new Intl.DateTimeFormat('en-US', {
  timeZone: 'America/New_York',
  year: 'numeric',
  month: 'numeric',
  day: 'numeric',
  hour: 'numeric',
  minute: 'numeric',
  hourCycle: 'h23',
  weekday: 'short',
});

const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

interface EasternTime {
  year: number;
  month: number; // 1-12
  day: number;
  weekday: number; // 0 = Sunday
  minutes: number; // since local midnight
}

function easternTime(ms: number): EasternTime {
  const parts: Record<string, string> = {};
  for (const p of eastern.formatToParts(new Date(ms))) parts[p.type] = p.value;
  return {
    year: Number(parts.year),
    month: Number(parts.month),
    day: Number(parts.day),
    weekday: WEEKDAYS.indexOf(parts.weekday),
    // `h23` can still print "24" for midnight on some engines; fold it back.
    minutes: (Number(parts.hour) % 24) * 60 + Number(parts.minute),
  };
}

/** Gregorian Easter (anonymous algorithm, Meeus/Jones/Butcher). */
export function easterSunday(year: number): { month: number; day: number } {
  const a = year % 19;
  const b = Math.floor(year / 100);
  const c = year % 100;
  const d = Math.floor(b / 4);
  const e = b % 4;
  const f = Math.floor((b + 8) / 25);
  const g = Math.floor((b - f + 1) / 3);
  const h = (19 * a + b - d - g + 15) % 30;
  const i = Math.floor(c / 4);
  const k = c % 4;
  const l = (32 + 2 * e + 2 * i - h - k) % 7;
  const m = Math.floor((a + 11 * h + 22 * l) / 451);
  const month = Math.floor((h + l - 7 * m + 114) / 31);
  const day = ((h + l - 7 * m + 114) % 31) + 1;
  return { month, day };
}

const weekdayOf = (y: number, m: number, d: number) => new Date(Date.UTC(y, m - 1, d)).getUTCDay();

/** Day of month of the nth (1-based) given weekday in a month. */
function nthWeekday(y: number, m: number, weekday: number, n: number): number {
  const first = weekdayOf(y, m, 1);
  return 1 + ((weekday - first + 7) % 7) + (n - 1) * 7;
}

function lastWeekday(y: number, m: number, weekday: number): number {
  const daysInMonth = new Date(Date.UTC(y, m, 0)).getUTCDate();
  const last = weekdayOf(y, m, daysInMonth);
  return daysInMonth - ((last - weekday + 7) % 7);
}

/**
 * A fixed-date holiday as the exchange observes it: a Saturday is taken on the
 * Friday before and a Sunday on the Monday after.
 */
function observed(y: number, m: number, d: number): { month: number; day: number } {
  const w = weekdayOf(y, m, d);
  if (w === 6) return { month: m, day: d - 1 };
  if (w === 0) return { month: m, day: d + 1 };
  return { month: m, day: d };
}

/** Full-day closures for a year, as "month-day" keys. */
function holidays(y: number): Set<string> {
  const out = new Set<string>();
  const add = (m: number, d: number) => out.add(`${m}-${d}`);

  // New Year's Day is the exception to the observance rule: a Saturday January
  // 1st is NOT taken on the Friday before (that Friday, December 31st, is the
  // last trading day of the previous year), so a Saturday adds nothing; only a
  // Sunday moves, to the Monday the 2nd.
  const newYearDay = weekdayOf(y, 1, 1);
  if (newYearDay === 0) add(1, 2);
  else if (newYearDay !== 6) add(1, 1);
  add(1, nthWeekday(y, 1, 1, 3)); // Martin Luther King Jr. Day
  add(2, nthWeekday(y, 2, 1, 3)); // Washington's Birthday
  const easter = easterSunday(y);
  const goodFriday = new Date(Date.UTC(y, easter.month - 1, easter.day - 2));
  add(goodFriday.getUTCMonth() + 1, goodFriday.getUTCDate());
  add(5, lastWeekday(y, 5, 1)); // Memorial Day
  if (y >= 2022) {
    const j = observed(y, 6, 19); // Juneteenth
    add(j.month, j.day);
  }
  const i = observed(y, 7, 4); // Independence Day
  add(i.month, i.day);
  add(9, nthWeekday(y, 9, 1, 1)); // Labor Day
  add(11, nthWeekday(y, 11, 4, 4)); // Thanksgiving
  const x = observed(y, 12, 25); // Christmas
  add(x.month, x.day);
  return out;
}

/** True when the regular session ends at 13:00 rather than 16:00. */
function isEarlyClose(t: EasternTime, closed: Set<string>): boolean {
  if (t.month === 11) return t.day === nthWeekday(t.year, 11, 4, 4) + 1; // the day after Thanksgiving
  const weekday = t.weekday >= 1 && t.weekday <= 5;
  if (!weekday || closed.has(`${t.month}-${t.day}`)) return false;
  return (t.month === 7 && t.day === 3) || (t.month === 12 && t.day === 24);
}

export function usEquitySession(nowMs: number): EquitySession {
  const t = easternTime(nowMs);

  if (t.weekday === 0 || t.weekday === 6) return { open: false, reason: 'weekend' };

  const closed = holidays(t.year);
  if (closed.has(`${t.month}-${t.day}`)) return { open: false, reason: 'holiday' };

  if (t.minutes < OPEN_MINUTES) return { open: false, reason: 'before-open' };
  const close = isEarlyClose(t, closed) ? EARLY_CLOSE_MINUTES : CLOSE_MINUTES;
  if (t.minutes >= close) return { open: false, reason: 'after-close' };
  return { open: true };
}

/** "3:59 PM ET" -- the Eastern clock time of an instant, for a closed-market chip. */
export function easternClockLabel(ms: number): string {
  const text = new Intl.DateTimeFormat('en-US', {
    timeZone: 'America/New_York',
    hour: 'numeric',
    minute: '2-digit',
  }).format(new Date(ms));
  return `${text} ET`;
}
