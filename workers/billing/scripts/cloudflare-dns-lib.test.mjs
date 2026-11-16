/**
 * Gate for cloudflare-dns-lib.mjs.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Every case here is a defect a review found in cloudflare-dns.mjs, and the
 * assertions are written against the WRONG answer rather than the right one,
 * because each of these bugs produced a confident wrong statement rather than a
 * failure: an expired token reported as a missing permission, a relative record
 * name reported as absent, a truncated listing reported as the whole zone. A
 * test that only checked the happy path would have passed against all three.
 *
 *     node --test scripts/
 */
import assert from "node:assert/strict";
import { describe, it } from "node:test";

import {
  PER_PAGE,
  classify,
  errText,
  fqdnIn,
  isPermissionVerdict,
  paginate,
  unquote,
} from "./cloudflare-dns-lib.mjs";

const ZONE = "mortgagefvcalculator.com";

/** A Cloudflare listing page. */
const page = (result, { total, pages } = {}) => ({
  status: 200,
  body: {
    result,
    result_info: {
      ...(total === undefined ? {} : { total_count: total }),
      ...(pages === undefined ? {} : { total_pages: pages }),
    },
  },
});

const cfErrors = (status, ...errors) => ({ status, body: { success: false, errors } });

describe("classify", () => {
  it("does not call an invalid or revoked token a missing permission", () => {
    // The defect: `msg.includes("permission")` plus a bare 403 meant a
    // credential problem sent the operator to the token-scopes UI.
    const invalid = classify(401, { errors: [{ code: 1000, message: "Invalid API Token" }] });
    assert.ok(
      !invalid.startsWith("DENIED"),
      `a 401 must not read as a permission problem, got ${invalid}`,
    );
    assert.match(invalid, /UNAUTHENTICATED/);
    assert.match(classify(403, { errors: [{ code: 6003, message: "Invalid request headers" }] }), /UNAUTHENTICATED/);
  });

  it("still names a real permission refusal", () => {
    // Non-vacuity: the verdict the tool exists to give is still given.
    const denied = classify(403, {
      errors: [{ code: 10000, message: "Unauthorized to access requested resource" }],
    });
    assert.match(denied, /DENIED/);
    assert.match(
      classify(403, { errors: [{ code: 10000, message: "You are not authorized" }] }),
      /DENIED/,
    );
  });

  it("says a bare 403 is ambiguous instead of guessing", () => {
    // Cloudflare's code 10000 is both "wrong token" and "token lacks the
    // permission". Claiming either one is a coin toss presented as a diagnosis.
    const v = classify(403, { errors: [{ code: 10000, message: "Authentication error" }] });
    assert.match(v, /FORBIDDEN/);
    assert.match(v, /both/);
  });

  it("keeps a transport failure, a 404 and a 429 out of the permission bucket", () => {
    // The defect behind the unconditional advice block: these all printed "the
    // permission is missing from the token", including a network blip.
    for (const [status, body] of [
      [0, "TypeError: fetch failed"],
      [404, { errors: [{ code: 7003, message: "Could not route to /zones" }] }],
      [429, { errors: [{ code: 971, message: "rate limited" }] }],
      [500, { errors: [] }],
    ]) {
      const v = classify(status, body);
      assert.ok(!isPermissionVerdict(v), `status ${status} must not be a permission verdict: ${v}`);
    }
    // Control: the two verdicts that SHOULD print the Zone:DNS:Read advice do.
    assert.ok(isPermissionVerdict(classify(403, { errors: [{ code: 10000, message: "not authorized" }] })));
    assert.ok(isPermissionVerdict(classify(403, { errors: [{ code: 10000, message: "Authentication error" }] })));
  });

  it("reports an authorized read as authorized", () => {
    assert.equal(classify(200, { result: [] }), "AUTHORIZED");
  });

  it("carries the error text without inventing any", () => {
    assert.equal(errText(cfErrors(403, { code: 10000, message: "Authentication error" }).body), "10000: Authentication error");
    assert.equal(errText("TypeError: fetch failed"), "TypeError: fetch failed");
  });
});

describe("fqdnIn", () => {
  it("qualifies a relative name, which is why duplicates were written", () => {
    // The defect: the LIST returns the qualified name and the POST takes the
    // relative one, so `_acme-challenge` never equalled
    // `_acme-challenge.<zone>` and a new record went in on every run.
    assert.equal(fqdnIn("_acme-challenge", ZONE), `_acme-challenge.${ZONE}`);
    assert.equal(fqdnIn("_acme-challenge." + ZONE, ZONE), `_acme-challenge.${ZONE}`);
    // Both spellings must land on one key, or the comparison is still broken.
    assert.equal(fqdnIn("_acme-challenge", ZONE), fqdnIn(`_acme-challenge.${ZONE}`, ZONE));
  });

  it("treats the apex and @ as the zone itself", () => {
    assert.equal(fqdnIn(ZONE, ZONE), ZONE);
    assert.equal(fqdnIn("@", ZONE), ZONE);
  });

  it("does not merge a different zone's name into this one", () => {
    // Non-vacuity in the other direction: qualifying must not make two
    // different records compare equal.
    assert.notEqual(fqdnIn("www", ZONE), fqdnIn("www", "example.com"));
    assert.equal(fqdnIn("www.example.com", ZONE), `www.example.com.${ZONE}`);
  });
});

describe("unquote", () => {
  it("matches the value as stored against the value as sent", () => {
    const sent = "google-site-verification=abc_DEF-123";
    assert.equal(unquote(`"${sent}"`), sent);
    assert.equal(unquote(sent), sent);
  });

  it("leaves an interior quote alone", () => {
    assert.equal(unquote('"a"b"'), 'a"b');
    assert.equal(unquote(""), "");
    assert.equal(unquote(undefined), "");
  });
});

describe("paginate", () => {
  const rows = (n, from = 0) =>
    Array.from({ length: n }, (_, i) => ({ id: `r${from + i}`, type: "TXT", content: `v${from + i}` }));

  it("reads every page of a zone larger than one page", () => {
    // The defect: per_page=100 with no follow-through reported 100 records for
    // a 150-record zone, and the write path checked against that same list.
    const pages = [page(rows(PER_PAGE), { total: 150, pages: 2 }), page(rows(50, PER_PAGE), { total: 150, pages: 2 })];
    return paginate((n) => Promise.resolve(pages[n - 1])).then((out) => {
      assert.equal(out.status, 200);
      assert.equal(out.rows.length, 150);
      assert.equal(out.short, null, "a complete read must not report short");
      assert.equal(out.rows.at(-1).id, "r149");
    });
  });

  it("does not inflate a listing that fits on one page", async () => {
    // Control: the fix must not turn a one-page answer into extra requests or
    // a phantom shortfall.
    let calls = 0;
    const out = await paginate((n) => {
      calls += 1;
      return Promise.resolve(page(rows(1), { total: 1, pages: 1 }));
    });
    assert.equal(calls, 1);
    assert.equal(out.rows.length, 1);
    assert.equal(out.short, null);
  });

  it("REPORTS a short read rather than returning it as the whole answer", async () => {
    // This is the assertion that makes "the zone has no such record" safe to
    // act on. Cloudflare says 150 exist; if only 100 arrive, a caller that
    // cannot tell will conclude the other 50 do not exist.
    const out = await paginate((n) => Promise.resolve(page(rows(PER_PAGE), { total: 150, pages: 1 })));
    assert.deepEqual(out.short, { got: 100, total: 150 });
  });

  it("surfaces a failure on a later page instead of a partial 200", async () => {
    const out = await paginate((n) =>
      Promise.resolve(n === 1 ? page(rows(PER_PAGE), { total: 150, pages: 2 }) : cfErrors(429, { code: 971, message: "rate limited" })),
    );
    assert.equal(out.status, 429);
    assert.match(classify(out.status, out.body), /RATE LIMITED/);
  });

  it("stops at the page ceiling and reports short rather than looping", async () => {
    // A wrong total_pages must not spin, and must not then pass as complete.
    let calls = 0;
    const out = await paginate((n) => {
      calls += 1;
      return Promise.resolve(page(rows(1), { total: 1_000_000, pages: 1_000_000 }));
    }, 3);
    assert.equal(calls, 3);
    assert.ok(out.short, "a read cut off by the ceiling must report short");
  });

  it("treats a response with no result_info as one complete page", async () => {
    const out = await paginate(() => Promise.resolve(page(rows(2))));
    assert.equal(out.rows.length, 2);
    assert.equal(out.short, null);
  });
});

describe("the duplicate write that started all this", () => {
  const existing = (name, value) => ({ type: "TXT", name, content: `"${value}"` });
  const VALUE = "google-site-verification=xyz";

  it("finds an existing TXT that sits on the second page", async () => {
    const pages = [
      page(Array.from({ length: PER_PAGE }, (_, i) => existing(ZONE, `other-${i}`)), { total: 101, pages: 2 }),
      page([existing(ZONE, VALUE)], { total: 101, pages: 2 }),
    ];
    const look = await paginate((n) => Promise.resolve(pages[n - 1]));
    assert.equal(look.short, null);
    const hit = look.rows.find((r) => unquote(r.content) === VALUE);
    assert.ok(hit, "the record exists and must be found, or a duplicate is written");
  });

  it("finds it under the relative name the write path was given", async () => {
    // The two bugs compose: the record is listed qualified, and the caller
    // asked for it relative. Both sides go through fqdnIn before comparing.
    const wanted = fqdnIn("_token", ZONE);
    const look = await paginate(() =>
      Promise.resolve(page([existing(`_token.${ZONE}`, VALUE)], { total: 1, pages: 1 })),
    );
    const hit = look.rows.find(
      (r) => fqdnIn(String(r.name), ZONE) === wanted && unquote(r.content) === VALUE,
    );
    assert.ok(hit, "a relative name must match the qualified record it refers to");
  });

  it("does not match a different value at the same name", async () => {
    // Non-vacuity: idempotence must not become "never write anything".
    const look = await paginate(() =>
      Promise.resolve(page([existing(ZONE, "a-different-token")], { total: 1, pages: 1 })),
    );
    assert.equal(
      look.rows.find((r) => unquote(r.content) === VALUE),
      undefined,
    );
  });
});
