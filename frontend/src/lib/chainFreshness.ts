/**
 * How old a chain print is, and whether it may be called LIVE.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * This is a plain function over plain values, deliberately, so the rule can be
 * tested without a DOM — the same reason the stores are shaped the way they
 * are. `OptionChain` supplies `nowMs` rather than reading the clock in here so
 * a test can state the instant instead of waiting for one.
 *
 * The rule exists because the option-chain cache TTL is 15 minutes. Before it
 * was raised, every rendered chain was at most 15 seconds old and a permanent
 * LIVE chip was very nearly true. It no longer is, and a LIVE dot over a
 * fifteen-minute-old quote is not a cosmetic problem: it is a claim about the
 * data that the data does not support.
 *
 * Two ways that claim can be made accidentally, both handled here:
 *
 * 1. **The viewer's clock is not the server's clock.** `fetched_at` is an
 *    absolute server timestamp, so age is a subtraction across two machines.
 *    A browser running BEHIND the server computes a *negative* age, and a
 *    naive `age < 60` reads negative as "very fresh" — so a client whose clock
 *    is twenty minutes slow would badge a genuinely stale chain LIVE. Only the
 *    skew direction that overstates freshness is dangerous, so a small
 *    tolerance absorbs benign sub-second skew and anything beyond it is
 *    treated as unknown, which renders DELAYED.
 *
 * 2. **Time passes without a re-render.** Age computed once at render is
 *    frozen at render. A chain fetched fresh renders LIVE and, with nothing
 *    else changing on the page, would stay LIVE for the full fifteen minutes.
 *    That is why the caller ticks `nowMs`; this function stays pure.
 */

/** Age below which a print may be called LIVE. */
export const LIVE_MAX_AGE_SECONDS = 60;

/**
 * Clock skew absorbed before a negative age is treated as unusable. Benign
 * skew between a browser and a server is sub-second; seconds of it is still
 * plausible, minutes of it is not, and minutes is the case that matters.
 */
export const CLOCK_SKEW_TOLERANCE_SECONDS = 5;

export interface ChainFreshness {
  /** Seconds since the backend obtained the print; null when unknowable. */
  ageSeconds: number | null;
  /** True only when the age is known AND within the live window. */
  isLive: boolean;
  /** Local-time "as of" for the DELAYED chip; '' when unknowable. */
  asOfTime: string;
}

export function chainFreshness(fetchedAt: string | null | undefined, nowMs: number): ChainFreshness {
  if (!fetchedAt) {
    return { ageSeconds: null, isLive: false, asOfTime: '' };
  }

  const fetchedMs = Date.parse(fetchedAt);
  if (Number.isNaN(fetchedMs)) {
    // An unparseable timestamp is a backend or wire problem. Failing to
    // DELAYED states less than the truth; failing to LIVE would state more.
    return { ageSeconds: null, isLive: false, asOfTime: '' };
  }

  const asOfTime = new Date(fetchedMs).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  });

  const ageSeconds = (nowMs - fetchedMs) / 1000;

  if (ageSeconds < -CLOCK_SKEW_TOLERANCE_SECONDS) {
    // The print is stamped in this viewer's future by more than skew explains,
    // so this clock cannot measure its age at all. Keep the timestamp — it is
    // still what the server said — and drop the claim.
    return { ageSeconds: null, isLive: false, asOfTime };
  }

  return { ageSeconds, isLive: ageSeconds < LIVE_MAX_AGE_SECONDS, asOfTime };
}
