/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The rule this file holds:
 *
 *   THIS PROJECT HAS NO APPLE INTEGRATION, AND NOTHING MAY QUIETLY ADD ONE.
 *
 * Sign in with Apple requires a paid Apple Developer account. This project does
 * not have one and is not buying one, and social login was dropped during the
 * Railway migration (docs/technical/MORTGAGEFV_RAILWAY_MIGRATION.md). Sign-up is
 * email.
 *
 * WHY A TEST RATHER THAN JUST DELETING IT, and this is the instructive part: the
 * button was ALREADY unreachable. It sat behind
 * `process.env.NEXT_PUBLIC_OAUTH_ENABLED === '1'`, which is set nowhere, so the
 * live site never rendered it -- measured, the production HTML contained zero
 * occurrences. It was invisible in exactly the way that keeps dead code alive.
 *
 * What it was NOT invisible to was CI. `.github/workflows/apple-client-secret.yml`
 * ran every night at 06:12 UTC to rotate a client secret for the account that
 * does not exist, and failed every night for at least twelve consecutive days.
 * A daily red run is worse than a missing check: it is the noise that hides the
 * REAL failure, and a repository whose CI is always red has no CI.
 *
 * So the cost of this dead code was never a rendered button. It was a permanently
 * failing pipeline plus a rotation script and a test suite maintaining a feature
 * nobody could use. A gate on the SOURCE is what stops that returning, because a
 * gate on the rendered output would have passed throughout -- the flag saw to
 * that.
 */
import { describe, it, expect } from "vitest";
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join } from "node:path";

const SRC = join(__dirname, "..");

const sourceFiles = (dir: string): string[] =>
  readdirSync(dir).flatMap((entry) => {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) return sourceFiles(full);
    return /\.(ts|tsx)$/.test(entry) && !/\.test\.tsx?$/.test(entry) ? [full] : [];
  });

describe("no Apple integration", () => {
  const files = sourceFiles(SRC);

  it("finds source files to check (positive control)", () => {
    // Without this, a broken walker would make every assertion below vacuous --
    // the ad-routes failure this suite already records, in a different costume.
    expect(files.length).toBeGreaterThan(20);
  });

  it("names no Apple OAuth provider anywhere in src/", () => {
    const offenders = files.filter((f) => {
      const text = readFileSync(f, "utf8");
      // Matches the provider literal and the API that consumes it. Deliberately
      // NOT a bare /apple/i: AAPL is a real ticker this calculator prices, and a
      // test that fires on it would be switched off within a week.
      return /['"`]apple['"`]/i.test(text) || /signInWithApple/i.test(text);
    });
    expect(offenders).toEqual([]);
  });

  it("renders no Sign in with Apple button", () => {
    const offenders = files.filter((f) =>
      /Sign\s+in\s+with\s+Apple/i.test(readFileSync(f, "utf8")),
    );
    expect(offenders).toEqual([]);
  });

  it("still prices AAPL, so the guard is narrow and not a blanket ban", () => {
    // The complementary assertion. A rule that cannot tell the ticker from the
    // OAuth provider would either miss the provider or break the calculator, and
    // only checking one direction cannot tell which.
    const mentionsTicker = files.some((f) => /\bAAPL\b/.test(readFileSync(f, "utf8")));
    expect(mentionsTicker).toBe(true);
  });
});
