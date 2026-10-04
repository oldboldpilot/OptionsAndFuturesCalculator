/**
 * Static assets, plus a reverse proxy for `/auth/v1/*`.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS EXISTS, AND WHY IT IS NOT OPTIONAL.
 *
 * supabase-js builds every auth request as `${supabaseUrl}/auth/v1/<path>` --
 * the prefix is hardcoded in the client and cannot be configured away.
 * `NEXT_PUBLIC_SUPABASE_URL` pointed straight at the self-hosted GoTrue, which
 * serves `/user`, `/recover` and `/token` at its ROOT and has no `/auth/v1`
 * mount at all. Measured against production:
 *
 *                      GoTrue root      GoTrue /auth/v1
 *     /health              200               404
 *     /settings            200               404
 *     /user                401               404
 *     /recover             405               404
 *
 * So EVERY browser auth call 404'd: sign-in, sign-up and password reset alike.
 * The reset was simply the one somebody tried. mortgagefvcalculator.com does
 * not have this defect because its `SUPABASE_URL` is `mfv-gateway`, which maps
 * the prefix -- the same table against that host returns 200/200/401.
 *
 * It survived because every server-side probe passes. `curl .../recover` is
 * GoTrue's real path and answers correctly; only a BROWSER appends `/auth/v1`.
 * This was found by driving a real recovery link through Playwright and reading
 * the console: `404 @ .../auth/v1/user` from inside `setSession`.
 *
 * WHY A WORKER RATHER THAN A SECOND SERVICE. mfv needs a gateway because it
 * also fronts PostgREST; this site needs one path mapped. Proxying here makes
 * auth SAME-ORIGIN, which additionally removes the CORS surface entirely --
 * including the preflight that `apikey` had been failing.
 *
 * A REDIRECT CANNOT DO THIS JOB. Browsers do not follow redirects on a CORS
 * preflight, so a Cloudflare redirect rule would break every POST before it was
 * sent. It has to be a proxy.
 *
 * COST OF THE `main` SCRIPT. wrangler.toml's header said adding one "would put a
 * Worker invocation in front of every asset request". That is no longer how the
 * assets binding behaves: with both `main` and `assets`, a request that matches
 * an asset is served by the asset layer WITHOUT invoking this script, and only
 * what does not match arrives here. The fallback below hands those to
 * `env.ASSETS.fetch`, so `html_handling` and `not_found_handling` keep working
 * exactly as they did.
 */

interface Env {
  ASSETS: { fetch: (request: Request) => Promise<Response> };
  /** Origin of the self-hosted GoTrue, no trailing slash. */
  GOTRUE_ORIGIN: string;
}

const AUTH_PREFIX = '/auth/v1';

/**
 * Headers that must NOT be forwarded upstream.
 *
 * `host` would make GoTrue see this site's hostname and build its own links
 * from it, and the hop-by-hop headers are meaningless across a new connection.
 */
const STRIP_REQUEST = new Set([
  'host',
  'connection',
  'keep-alive',
  'transfer-encoding',
  'upgrade',
  'proxy-authorization',
  'proxy-authenticate',
  'te',
  'trailer',
]);

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (!url.pathname.startsWith(`${AUTH_PREFIX}/`) && url.pathname !== AUTH_PREFIX) {
      return env.ASSETS.fetch(request);
    }

    const origin = (env.GOTRUE_ORIGIN || '').replace(/\/$/, '');
    if (!origin) {
      // An unconfigured proxy must REFUSE rather than fall through to the asset
      // layer, which would answer an auth call with the 404 page -- a response
      // supabase-js reports as a transport failure, i.e. the original bug
      // wearing a different status code.
      return new Response('auth proxy is not configured (GOTRUE_ORIGIN unset)', {
        status: 503,
        headers: { 'content-type': 'text/plain; charset=utf-8' },
      });
    }

    // Strip the prefix supabase-js adds; GoTrue serves these at its root.
    const upstreamPath = url.pathname.slice(AUTH_PREFIX.length) || '/';
    const upstream = new URL(origin + upstreamPath + url.search);

    const headers = new Headers();
    for (const [k, v] of request.headers) {
      if (!STRIP_REQUEST.has(k.toLowerCase())) headers.set(k, v);
    }
    // GoTrue builds the links in its own emails from X-Forwarded-* when present,
    // and its redirect_to validation reads the real client origin.
    headers.set('X-Forwarded-Host', url.host);
    headers.set('X-Forwarded-Proto', url.protocol.replace(':', ''));

    const init: RequestInit = {
      method: request.method,
      headers,
      redirect: 'manual',
    };
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      init.body = request.body;
    }

    const upstreamResponse = await fetch(upstream.toString(), init);

    const out = new Headers(upstreamResponse.headers);

    // THE ENTITY HEADERS MUST GO, AND THIS IS NOT HOUSEKEEPING.
    //
    // The Workers runtime DECOMPRESSES an upstream response body transparently,
    // so what reaches here is plain bytes -- while `content-encoding: gzip` is
    // still sitting in the headers I copied. The browser then tries to gunzip
    // JSON that is already JSON, and supabase-js reports
    //
    //   Unexpected non-whitespace character after JSON at position 4
    //
    // which reads as a malformed response from the auth server and is entirely
    // this proxy's doing. `content-length` goes for the same reason: it
    // describes the COMPRESSED length and no longer matches the body.
    out.delete('content-encoding');
    out.delete('content-length');
    out.delete('transfer-encoding');
    out.delete('connection');

    // Same-origin from the browser's point of view, so no CORS headers are
    // needed or wanted -- and GoTrue sends `Access-Control-Allow-Origin: *`,
    // which browsers reject in combination with credentials. Drop it and let
    // same-origin do its job.
    out.delete('access-control-allow-origin');
    out.delete('access-control-allow-credentials');

    return new Response(upstreamResponse.body, {
      status: upstreamResponse.status,
      statusText: upstreamResponse.statusText,
      headers: out,
    });
  },
};
