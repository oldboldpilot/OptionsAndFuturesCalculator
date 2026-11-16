/**
 * The decisions cloudflare-dns.mjs makes, separated from the requests it makes.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY A MODULE RATHER THAN FOUR FUNCTIONS IN THE SCRIPT. Every defect a review
 * found in that script was in one of these four answers -- "is this refusal a
 * permission problem", "are these two record names the same name", "is this
 * listing the whole listing" -- and none of them could be tested, because the
 * script runs its requests at import time. Copying the functions into a test
 * would have made two sources of truth for one rule, with the drift invisible:
 * the test would keep passing against a rule the script no longer used. So the
 * rules live here, the script imports them, and the test imports the same file.
 *
 * Nothing here performs I/O or reads the environment. paginate() takes the page
 * fetcher as an argument for exactly that reason.
 */

/** Cloudflare's error array, flattened; never a token, which is not in it. */
export const errText = (body) =>
  Array.isArray(body?.errors) && body.errors.length
    ? body.errors.map((e) => `${e.code}: ${e.message}`).join("; ")
    : typeof body === "string"
      ? body
      : JSON.stringify(body).slice(0, 200);

/**
 * The distinction the tool exists for -- and the one it used to blur.
 *
 * `msg.includes("permission")` fired on any error text carrying the word, and a
 * bare `status === 403` on every refusal, so an EXPIRED OR REVOKED token was
 * reported as "the permission is missing from the token" and the operator was
 * sent to the token-scopes UI for a credential problem. Cloudflare's code 10000
 * "Authentication error" genuinely does not distinguish the two, so where it is
 * all we have the answer says it is ambiguous rather than picking the
 * likelier-sounding half.
 */
export function classify(status, body) {
  if (status === 200) return "AUTHORIZED";
  if (status === 0) return "UNREACHABLE (the request never completed)";
  const codes = Array.isArray(body?.errors) ? body.errors.map((e) => Number(e.code)) : [];
  const msg = errText(body).toLowerCase();
  if (status === 401 || codes.includes(1000) || codes.includes(6003))
    return "UNAUTHENTICATED (the token is invalid, expired or revoked)";
  if (/not authorized|unauthorized to access|insufficient permission|permission denied/.test(msg))
    return "DENIED (permission missing from the token)";
  if (status === 403)
    return "FORBIDDEN (a missing permission or an invalid token -- code 10000 is both)";
  if (status === 404) return "NOT FOUND";
  if (status === 429) return "RATE LIMITED";
  return `HTTP ${status}`;
}

/** True where the advice about Zone:DNS:Read is actually the diagnosis. */
export const isPermissionVerdict = (verdict) =>
  verdict.startsWith("DENIED") || verdict.startsWith("FORBIDDEN");

/**
 * One record name, spelled the one way.
 *
 * Cloudflare's LIST returns `name` fully qualified while its POST accepts a
 * relative one, so the two spellings of a single record never compared equal:
 * DNS_TXT_NAME=_acme-challenge looked absent beside _acme-challenge.<zone>, and
 * a duplicate went in on every single run.
 */
export const fqdnIn = (name, zone) =>
  name === "@" ? zone : name === zone || name.endsWith(`.${zone}`) ? name : `${name}.${zone}`;

/** A TXT value comes back from the API quoted and goes out unquoted. */
export const unquote = (v) => String(v ?? "").replace(/^"([\s\S]*)"$/, "$1");

/** How many pages a listing of this size needs, for the fetcher's benefit. */
export const PER_PAGE = 100;

/**
 * Every page of a listing, not just the first.
 *
 * `per_page=100` with nothing following it made two separate claims wrong on a
 * zone over 100 records: the printed count was the page size rather than the
 * zone's, and the idempotency check read that same truncated list, so an
 * existing TXT sitting on page 2 read as absent and a duplicate was written.
 *
 * result_info carries the real total, so a short read is REPORTED rather than
 * returned as if it were the whole answer. That matters more than completing
 * the read: a caller that cannot see it is short will happily conclude "no such
 * record" from a list that simply stopped early. The 50-page ceiling is there
 * so a wrong total_pages cannot spin forever, and tripping it reports short
 * like any other incomplete read.
 *
 * `fetchPage(page)` returns `{ status, body }`.
 */
export async function paginate(fetchPage, maxPages = 50) {
  const rows = [];
  let page = 1;
  let pages = 1;
  let total = null;
  let last = null;
  do {
    last = await fetchPage(page);
    if (last.status !== 200) return { status: last.status, body: last.body, rows, short: null };
    rows.push(...(last.body?.result ?? []));
    const info = last.body?.result_info ?? {};
    pages = Number(info.total_pages ?? 1) || 1;
    total = info.total_count === undefined ? null : Number(info.total_count);
    page += 1;
  } while (page <= pages && page <= maxPages);
  const short = total !== null && rows.length < total ? { got: rows.length, total } : null;
  return { status: 200, body: last.body, rows, short };
}
