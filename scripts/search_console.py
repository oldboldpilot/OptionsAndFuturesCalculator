#!/usr/bin/env python3
"""Google Search Console API tool for optionsandfuturescalculator.com and
mortgagefvcalculator.com -- standard library only, no credentials printed.

@author Olumuyiwa Oluwasanmi

The OAuth client is the "installed app" JSON in config/client_secret_*.json
(gitignored). One interactive consent mints a refresh token, saved 0600 to
config/google_search_console_token.json (gitignored); every later call
refreshes an access token from it without a browser.

    # 1. Consent -- EITHER on this machine (a local listener catches Google's redirect):
    python3 scripts/search_console.py auth --listen
    #    OR from any device: open the printed link, approve, then copy the
    #    address of the page Google redirects to (it will fail to load -- that
    #    is expected) and pass it back:
    python3 scripts/search_console.py auth-url
    python3 scripts/search_console.py auth --redirect 'http://localhost:8765/?code=...'

    # 2. Use it
    python3 scripts/search_console.py sites
    python3 scripts/search_console.py sitemaps SITE
    python3 scripts/search_console.py submit-sitemap SITE SITEMAP_URL
    python3 scripts/search_console.py delete-sitemap SITE SITEMAP_URL
    python3 scripts/search_console.py inspect SITE [--sitemap URL] [--limit N] [--out FILE]

SITE is the Search Console property exactly as `sites` lists it, e.g.
"sc-domain:mortgagefvcalculator.com" or "https://mortgagefvcalculator.com/".

Nothing here ever prints the client secret, an auth code, or a token.
"""
from __future__ import annotations

import argparse
import datetime
import base64
import concurrent.futures as cf
import glob
import hashlib
import http.server
import json
import os
import re
import secrets
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, "config")
TOKEN_PATH = os.path.join(CONFIG, "google_search_console_token.json")
# PKCE verifier between `auth-url` and `auth --redirect`. Outside the repo:
# it is a one-login secret and has no business in any tree that gets synced.
STATE_PATH = os.path.expanduser("~/.cache/search_console_oauth_state.json")
# TWO scopes, and the second one is why a re-consent is needed after this change.
#
# `webmasters` covers the Search Console surface: properties, sitemaps, search
# analytics. It does NOT cover ownership verification, which lives in a separate
# product (Site Verification) with its own scope. Adding a DOMAIN property is a
# webmasters call and succeeds while leaving the property siteUnverifiedUser; the
# DNS TXT token that turns it into siteOwner can only be minted with
# `siteverification`.
#
# A refresh token carries the scopes it was granted, so widening this list does
# not widen an existing token -- the next call fails with insufficient scope
# until `auth` is run again. That is the correct behaviour and worth the one
# interactive step, because it makes verification scriptable for every future
# domain rather than a hunt through a UI that never prompted.
SCOPE = " ".join([
    "https://www.googleapis.com/auth/webmasters",
    "https://www.googleapis.com/auth/siteverification",
])
PORT = 8765
REDIRECT_URI = f"http://localhost:{PORT}/"
API = "https://www.googleapis.com/webmasters/v3"
INSPECT_API = "https://searchconsole.googleapis.com/v1/urlInspection/index:inspect"
VERIFY_API = "https://www.googleapis.com/siteVerification/v1"


def die(msg: str) -> None:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def client() -> dict:
    """The OAuth client to use. SEARCH_CONSOLE_CLIENT (a path, or a substring of
    the filename) picks one explicitly; otherwise the most recently added
    config/client_secret_*.json wins -- when a new client is dropped in to
    replace an old one, the new one is the one meant."""
    found = glob.glob(os.path.join(CONFIG, "client_secret_*.json"))
    if not found:
        die(f"no OAuth client in {CONFIG}/client_secret_*.json")
    want = os.environ.get("SEARCH_CONSOLE_CLIENT", "")
    if want:
        found = [f for f in found if want in f] or die(f"no client matches SEARCH_CONSOLE_CLIENT={want!r}")
    path = max(found, key=os.path.getmtime)
    with open(path) as f:
        c = json.load(f)
    inner = c.get("installed") or c.get("web") or die("unrecognised client JSON")
    print(f"(OAuth client: project {inner.get('project_id')})", file=sys.stderr)
    return inner  # type: ignore[return-value]


def write_private(path: str, data: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(data, f)


def stored_scopes() -> set:
    """The scopes on the token already saved, or an empty set if there is none."""
    try:
        with open(TOKEN_PATH) as f:
            return set((json.load(f).get("scope") or "").split())
    except (OSError, ValueError):
        return set()


def site_key(site: str) -> str:
    """A property string normalised for comparison.

    Google returns a url-prefix property with a trailing slash whatever was
    sent, so "https://example.com" and "https://example.com/" are one property
    spelled two ways. A domain property has no such slack and is left alone.
    """
    return site if site.startswith("sc-domain:") else site.rstrip("/") + "/"


def bare_domain(d: str) -> str:
    """The Site Verification identifier for a domain, or a fatal error.

    Site Verification names the DOMAIN; Search Console names a PROPERTY, and
    its domain properties are spelled "sc-domain:example.com". The two are easy
    to confuse because `sites` prints the property form, which is therefore the
    string that gets copied. Passing it through mints a token for a domain
    literally called "sc-domain:example.com": the API accepts it, the TXT record
    publishes fine, and verification then never succeeds with nothing anywhere
    saying why. Both docstrings here warned about that trap and neither command
    enforced it, so it is refused rather than described -- with the corrected
    value printed, because the whole failure is a copy-paste.
    """
    if d.startswith("sc-domain:"):
        die(f"that is the Search Console property form. Pass the bare domain: "
            f"{d[len('sc-domain:'):]}")
    if "://" in d:
        host = urllib.parse.urlsplit(d).hostname or d
        die(f"that is a URL-prefix property. Pass the bare domain: {host}")
    if "/" in d:
        die(f"a domain carries no path. Pass the bare domain: {d.split('/')[0]}")
    return d


def int_at_least(lo: int):
    """An argparse type that refuses a value the derived window cannot survive.

    `--days 0` makes start one day AFTER end (an inverted window, reported by
    the API as a generic 400 that reads as a request bug), and a negative
    `--lag` queries dates in the future, which return no rows and read as a
    traffic collapse -- the exact misreading the lag default exists to prevent.
    """
    def parse(v: str) -> int:
        try:
            n = int(v)
        except ValueError:
            raise argparse.ArgumentTypeError(f"{v!r} is not an integer")
        if n < lo:
            raise argparse.ArgumentTypeError(f"must be {lo} or more, got {n}")
        return n
    return parse


def post_form(url: str, fields: dict) -> dict:
    body = urllib.parse.urlencode(fields).encode()
    req = urllib.request.Request(url, data=body, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        # Google's token errors carry no secret -- "invalid_grant" etc.
        detail = json.loads(e.read() or b"{}")
        die(f"token endpoint {e.code}: {detail.get('error')} {detail.get('error_description', '')}")
        raise


# ------------------------------------------------------------------ consent


def auth_url() -> str:
    c = client()
    verifier = secrets.token_urlsafe(64)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=")
    state = secrets.token_urlsafe(16)
    write_private(STATE_PATH, {"verifier": verifier, "state": state})
    q = {
        "client_id": c["client_id"],
        "redirect_uri": REDIRECT_URI,
        "response_type": "code",
        "scope": SCOPE,
        "access_type": "offline",
        "prompt": "consent",
        "code_challenge": challenge.decode(),
        "code_challenge_method": "S256",
        "state": state,
    }
    return c["auth_uri"] + "?" + urllib.parse.urlencode(q)


def exchange(redirected: str) -> None:
    if not os.path.exists(STATE_PATH):
        die("no pending login -- run `auth-url` (or `auth --listen`) first")
    with open(STATE_PATH) as f:
        st = json.load(f)
    params = urllib.parse.parse_qs(urllib.parse.urlsplit(redirected).query)
    if "error" in params:
        die(f"Google refused consent: {params['error'][0]}")
    if params.get("state", [""])[0] != st["state"]:
        die("state mismatch -- that redirect is from a different login attempt; run auth-url again")
    code = params.get("code", [""])[0] or die("no ?code= in that URL")
    c = client()
    tok = post_form(
        c["token_uri"],
        {
            "code": code,
            "client_id": c["client_id"],
            "client_secret": c["client_secret"],
            "redirect_uri": REDIRECT_URI,
            "grant_type": "authorization_code",
            "code_verifier": st["verifier"],
        },
    )
    if "refresh_token" not in tok:
        die("no refresh token returned; revoke the app's access in your Google account and retry")
    now = int(time.time())
    tok["obtained_at"] = now
    # CONSENT TIME IS A SEPARATE KEY, because obtained_at cannot answer for it.
    # access_token() rewrites obtained_at on every refresh -- it has to, since
    # expires_in is measured from it -- so on any token that has been used it
    # records the age of the last ACCESS token, minutes old, not the age of the
    # REFRESH token. Reading the 7-day Testing-mode expiry off it meant the
    # warning was dead code for precisely the tokens in daily use.
    tok["consented_at"] = now

    # CHECK WHAT WAS ACTUALLY GRANTED, AND SAY SO -- BEFORE STORING IT.
    #
    # Google returns the granted scopes in the token response, and they are not
    # necessarily the scopes requested: a consent screen approved before this
    # tool's SCOPE list grew yields a token that is valid, refreshes happily,
    # and then fails one specific call with "Request had insufficient
    # authentication scopes" -- hundreds of lines away from the cause, with
    # nothing pointing back at the consent. That happened: a token minted at
    # 23:50 could not mint a verification token because the scope was widened
    # at 00:08, and the only visible symptom was a 403 on an unrelated-looking
    # command. Comparing here turns an invisible staleness into one line.
    # The order matters and used to be wrong. write_private() is destructive and
    # what it destroys needs another interactive consent to get back, so a
    # half-scope token -- one unticked box on the consent screen -- replaced a
    # fully scoped one, and the command still exited 0 for anything reading the
    # status. Now the comparison decides whether to write at all.
    granted = set((tok.get("scope") or "").split())
    wanted = set(SCOPE.split())
    missing = sorted(wanted - granted)
    held = stored_scopes()
    rel = os.path.relpath(TOKEN_PATH, ROOT)
    print(f"scopes granted: {len(granted)}")
    for s in sorted(granted):
        print(f"  {s}")
    if missing:
        print()
        print("WARNING: the consent screen did NOT grant every requested scope:")
        for s in missing:
            print(f"  MISSING {s}")
        lost = sorted(held - granted)
        if lost:
            print(f"The token already in {rel} holds {len(lost)} scope(s) this one")
            print("does not, so it was LEFT IN PLACE rather than overwritten:")
            for s in lost:
                print(f"  KEPT {s}")
        else:
            write_private(TOKEN_PATH, tok)
            print(f"Saved to {rel} anyway -- it loses nothing the previous token had.")
        os.remove(STATE_PATH)
        print("Re-run `auth` and approve ALL permissions it asks for. Until then the")
        print("calls needing those scopes fail with 403 insufficient scopes.")
        sys.exit(1)
    write_private(TOKEN_PATH, tok)
    os.remove(STATE_PATH)
    print(f"ok: token saved to {rel} (0600, gitignored)")


def listen() -> None:
    url = auth_url()
    print("Open this link in a browser ON THIS MACHINE and approve access:\n")
    print(url + "\n")
    print(f"Waiting on {REDIRECT_URI} for Google's redirect (15 minutes)...")
    got: dict = {}

    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            got["url"] = f"http://localhost:{PORT}{self.path}"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"Search Console access received. You can close this tab.")

        def log_message(self, *a):  # silence: the request line carries the code
            pass

    srv = http.server.HTTPServer(("127.0.0.1", PORT), H)
    srv.timeout = 900  # 5 minutes timed out twice in practice
    srv.handle_request()
    if "url" not in got:
        die("timed out waiting for the redirect")
    exchange(got["url"])


# ---------------------------------------------------------------- API calls


def access_token() -> str:
    if not os.path.exists(TOKEN_PATH):
        die("not authorised yet -- run `auth --listen` or `auth-url` first")
    with open(TOKEN_PATH) as f:
        tok = json.load(f)
    if tok.get("access_token") and time.time() < tok.get("obtained_at", 0) + tok.get("expires_in", 0) - 120:
        return tok["access_token"]
    c = client()
    new = post_form(
        c["token_uri"],
        {
            "client_id": c["client_id"],
            "client_secret": c["client_secret"],
            "refresh_token": tok["refresh_token"],
            "grant_type": "refresh_token",
        },
    )
    tok.update(new)
    tok["obtained_at"] = int(time.time())
    write_private(TOKEN_PATH, tok)
    return tok["access_token"]


def call(method: str, url: str, body: dict | None = None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    for attempt in range(5):
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Authorization", f"Bearer {access_token()}")
        if data is not None:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                raw = r.read()
                return json.loads(raw) if raw else {}
        except urllib.error.HTTPError as e:
            if e.code in (429, 500, 503) and attempt < 4:
                time.sleep(2 ** attempt * 2)
                continue
            msg = json.loads(e.read() or b"{}").get("error", {})
            die(f"{method} {url.split('?')[0]} -> {e.code}: {msg.get('status', '')} {msg.get('message', '')}")
    return {}


def enc(site: str) -> str:
    return urllib.parse.quote(site, safe="")


def cmd_sites(_: argparse.Namespace) -> None:
    for s in call("GET", f"{API}/sites").get("siteEntry", []):
        print(f"{s['siteUrl']}\t{s['permissionLevel']}")


def cmd_verify_token(a: argparse.Namespace) -> None:
    """Mint the DNS TXT token that verifies ownership of a DOMAIN property.

    Search Console shows this string in its own add-property wizard. A property
    added through the API never runs that wizard, so the UI offers nowhere to
    read it -- which is exactly the dead end this command exists for.

    `identifier` is the bare domain, NOT the "sc-domain:" form: Site Verification
    is a different product and names the thing itself rather than the Search
    Console property. Passing the prefixed form mints a token for a domain
    literally called "sc-domain:example.com", which then never verifies.

    The token is PUBLIC by design -- it is published in DNS for the world to
    read -- so printing it is the point.
    """
    domain = bare_domain(a.domain)
    body = {
        "site": {"type": "INET_DOMAIN", "identifier": domain},
        "verificationMethod": "DNS_TXT",
    }
    out = call("POST", f"{VERIFY_API}/token", body)
    tok = out.get("token", "")
    if not tok:
        die(f"no token returned: {json.dumps(out)[:300]}")
    print(f"domain: {domain}")
    print(f"record: TXT @ (the zone apex)")
    print(f"value:  {tok}")
    print()
    print("Add that as a TXT record at the apex, wait for it to resolve, then:")
    print(f"  scripts/search_console.py verify-domain {domain}")


def cmd_verify_domain(a: argparse.Namespace) -> None:
    """Ask Google to check the DNS TXT record and take ownership.

    Fails until the record is visible to Google's resolvers, which is a
    propagation question and not an error in the request -- the message says
    which.
    """
    domain = bare_domain(a.domain)
    body = {"site": {"type": "INET_DOMAIN", "identifier": domain}}
    out = call(
        "POST",
        f"{VERIFY_API}/webResource?verificationMethod=DNS_TXT",
        body,
    )
    owners = out.get("owners", [])
    print(f"verified: {out.get('id', domain)}")
    if owners:
        print(f"  owners: {len(owners)}")
    print("Re-run `sites`; the domain property should now read siteOwner.")


def cmd_scopes(_: argparse.Namespace) -> None:
    """What the stored token is actually allowed to do.

    Cheaper and clearer than discovering it as a 403 on whichever command
    happens to need the missing scope.
    """
    if not os.path.exists(TOKEN_PATH):
        die("no token stored -- run `auth --listen`")
    with open(TOKEN_PATH) as f:
        tok = json.load(f)
    granted = set((tok.get("scope") or "").split())
    wanted = set(SCOPE.split())
    # consented_at, NOT obtained_at: see exchange(). obtained_at is rewritten on
    # every refresh, so reading the refresh token's age off it reported "0.0 days
    # ago" for a token consented weeks earlier and skipped the note below.
    consented = int(tok.get("consented_at", 0))
    age_days = (int(time.time()) - consented) / 86400 if consented else None
    if age_days is None:
        print("consent date not recorded (this token predates consented_at)")
    else:
        print(f"consent granted {age_days:.1f} days ago")
    print(f"granted {len(granted)} scope(s):")
    for s in sorted(granted):
        print(f"  {s}")
    missing = sorted(wanted - granted)
    if missing:
        print("MISSING, so the matching commands will 403:")
        for s in missing:
            print(f"  {s}")
        print("Re-run `auth --listen` and approve every permission shown.")
    else:
        print("every scope this tool needs is granted.")
    # Printed when the age is UNKNOWN as well as when it is over six days: the
    # cost of the note when it does not apply is three lines, and the cost of
    # withholding it is a refresh chain that dies with invalid_grant and no clue.
    if age_days is None or age_days > 6:
        print()
        if age_days is None:
            print("(the consent date is unknown, so this cannot be ruled out)")
        print("NOTE: a refresh token from an OAuth app still in 'Testing' publishing")
        print("status expires after 7 days. If refreshes start failing with")
        print("invalid_grant, publish the app rather than re-consenting weekly.")


def cmd_add_site(a: argparse.Namespace) -> None:
    """Add a property to this account's Search Console set.

    A URL-PREFIX property ("https://example.com/") covers exactly that scheme and
    host: traffic to www, to http, or to any other subdomain is invisible to it.
    A DOMAIN property ("sc-domain:example.com") covers every subdomain and both
    schemes, which is why a reported click count from a url-prefix property is a
    lower bound rather than a measurement.

    This call only ADDS the property; it does not verify it. A domain property is
    verifiable only by DNS TXT, and the token for that comes from the Site
    Verification API -- which this tool now does request a scope for, so the next
    step is `verify-token`, named here rather than left to a UI that does not
    show it for a property added this way.
    """
    call("PUT", f"{API}/sites/{enc(a.site)}")
    print(f"added: {a.site}")
    # Compared on a NORMALISED key, because Google does not store back what was
    # sent: a url-prefix property given without a trailing slash is listed with
    # one, so an exact match reported a successful add as "not listed yet" and
    # skipped the siteUnverifiedUser branch that says verification is still owed.
    for s in call("GET", f"{API}/sites").get("siteEntry", []):
        if site_key(s["siteUrl"]) == site_key(a.site):
            print(f"  listed as: {s['siteUrl']}")
            print(f"  permission now: {s['permissionLevel']}")
            if s["permissionLevel"] == "siteUnverifiedUser":
                print("  NOT VERIFIED yet.")
                if a.site.startswith("sc-domain:"):
                    bare = a.site[len("sc-domain:"):]
                    print("  A domain property verifies by DNS TXT and nothing else, and the")
                    print("  value is not shown in the UI for a property added through the API:")
                    print(f"    scripts/search_console.py verify-token {bare}")
                    print(f"    scripts/search_console.py verify-domain {bare}")
                else:
                    host = urllib.parse.urlsplit(a.site).hostname or a.site
                    print("  A url-prefix property also verifies by HTML file or meta tag in")
                    print("  the UI. To do it by DNS instead -- which verifies the whole domain")
                    print("  and so covers this property too:")
                    print(f"    scripts/search_console.py verify-token {host}")
                    print(f"    scripts/search_console.py verify-domain {host}")
            return
    print("  (not listed back yet; re-run `sites` in a moment)")


def cmd_query(a: argparse.Namespace) -> None:
    """Search Analytics: clicks, impressions, CTR and position over a window.

    WHY THIS EXISTS. The rest of this tool answers "is the site indexed" --
    sitemaps and URL inspection. It could not answer "does anyone arrive", which
    is the question that decides whether a monetisation problem is a conversion
    problem or a traffic problem. Those have opposite responses, so guessing
    between them is expensive.

    The API caps a response at 25,000 rows and reports the LAST ~2 days
    incompletely, so a window ending today understates. `--days` therefore ends
    the window 3 days back by default rather than at today, and the dates used
    are printed so a number can be re-taken against the same window.
    """
    end = datetime.date.today() - datetime.timedelta(days=a.lag)
    start = end - datetime.timedelta(days=a.days - 1)
    body: dict = {
        "startDate": start.isoformat(),
        "endDate": end.isoformat(),
        "rowLimit": a.limit,
    }
    if a.dimension:
        body["dimensions"] = [a.dimension]
    out = call("POST", f"{API}/sites/{enc(a.site)}/searchAnalytics/query", body)
    rows = out.get("rows", [])
    print(f"{a.site}  {start} .. {end}  ({a.days} days, ending {a.lag} days back)")
    if not rows:
        print("  no rows -- the property has no recorded search traffic in this window")
        return
    if not a.dimension:
        r = rows[0]
        print(f"  clicks {int(r.get('clicks', 0)):>8}")
        print(f"  impressions {int(r.get('impressions', 0)):>3}")
        print(f"  ctr {r.get('ctr', 0) * 100:>11.2f}%")
        print(f"  position {r.get('position', 0):>8.1f}")
        return
    print(f"  {'KEY':<54} {'CLICKS':>7} {'IMPR':>8} {'CTR':>7} {'POS':>6}")
    for r in rows:
        key = (r.get("keys") or ["(none)"])[0]
        if len(key) > 54:
            key = key[:51] + "..."
        print(
            f"  {key:<54} {int(r.get('clicks', 0)):>7} {int(r.get('impressions', 0)):>8} "
            f"{r.get('ctr', 0) * 100:>6.2f}% {r.get('position', 0):>6.1f}"
        )
    total_c = sum(int(r.get("clicks", 0)) for r in rows)
    total_i = sum(int(r.get("impressions", 0)) for r in rows)
    print(f"  {'-- shown rows':<54} {total_c:>7} {total_i:>8}")
    if len(rows) == a.limit:
        print(f"  (exactly {a.limit} rows returned: raise --limit, the window may hold more)")


def cmd_sitemaps(a: argparse.Namespace) -> None:
    for s in call("GET", f"{API}/sites/{enc(a.site)}/sitemaps").get("sitemap", []):
        contents = ", ".join(
            f"{c.get('type')}: submitted {c.get('submitted')} indexed {c.get('indexed', '?')}"
            for c in s.get("contents", [])
        )
        print(
            f"{s['path']}\tlastSubmitted={s.get('lastSubmitted')}\tlastDownloaded={s.get('lastDownloaded')}"
            f"\terrors={s.get('errors')}\twarnings={s.get('warnings')}\tpending={s.get('isPending')}\t{contents}"
        )


def cmd_submit(a: argparse.Namespace) -> None:
    call("PUT", f"{API}/sites/{enc(a.site)}/sitemaps/{enc(a.sitemap)}")
    print(f"submitted {a.sitemap}")


def cmd_delete(a: argparse.Namespace) -> None:
    call("DELETE", f"{API}/sites/{enc(a.site)}/sitemaps/{enc(a.sitemap)}")
    print(f"deleted {a.sitemap}")


def sitemap_urls(url: str) -> list[str]:
    # Cloudflare answers 403 to Python's default User-Agent on both sites.
    req = urllib.request.Request(url, headers={"User-Agent": "search-console-tool/1.0 (+site owner)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        xml = r.read().decode()
    locs = re.findall(r"<loc>([^<]+)</loc>", xml)
    if "<sitemapindex" in xml:
        out: list[str] = []
        for child in locs:
            out += sitemap_urls(child)
        return out
    return locs


def cmd_inspect(a: argparse.Namespace) -> None:
    urls = sitemap_urls(a.sitemap) if a.sitemap else [l.strip() for l in sys.stdin if l.strip()]
    if a.limit:
        urls = urls[: a.limit]

    def one(u: str) -> dict:
        r = call("POST", INSPECT_API, {"inspectionUrl": u, "siteUrl": a.site, "languageCode": "en-US"})
        idx = r.get("inspectionResult", {}).get("indexStatusResult", {})
        return {
            "url": u,
            "verdict": idx.get("verdict"),
            "coverageState": idx.get("coverageState"),
            "indexingState": idx.get("indexingState"),
            "robotsTxtState": idx.get("robotsTxtState"),
            "pageFetchState": idx.get("pageFetchState"),
            "googleCanonical": idx.get("googleCanonical"),
            "userCanonical": idx.get("userCanonical"),
            "lastCrawlTime": idx.get("lastCrawlTime"),
            "crawledAs": idx.get("crawledAs"),
            "sitemap": idx.get("sitemap"),
            "referringUrls": idx.get("referringUrls"),
        }

    # The URL Inspection API allows 600 calls a minute and 2,000 a day per
    # property; four workers stays well inside the per-minute limit.
    results = []
    with cf.ThreadPoolExecutor(4) as ex:
        for i, r in enumerate(ex.map(one, urls), 1):
            results.append(r)
            if i % 50 == 0:
                print(f"  inspected {i}/{len(urls)}", file=sys.stderr)
    if a.out:
        with open(a.out, "w") as f:
            json.dump(results, f, indent=1)
    counts: dict = {}
    for r in results:
        counts[r["coverageState"]] = counts.get(r["coverageState"], 0) + 1
    print(f"{len(results)} URLs inspected on {a.site}")
    for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {v:5d}  {k}")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("auth-url")
    au = sub.add_parser("auth")
    g = au.add_mutually_exclusive_group(required=True)
    g.add_argument("--listen", action="store_true")
    g.add_argument("--redirect")
    sub.add_parser("sites")
    sub.add_parser("scopes", help="what the stored token is allowed to do")
    asite = sub.add_parser("add-site", help="add a property (does not verify it)")
    asite.add_argument("site")
    vt = sub.add_parser("verify-token", help="mint the DNS TXT value for a domain property")
    vt.add_argument("domain", help="bare domain, NOT the sc-domain: form")
    vd = sub.add_parser("verify-domain", help="ask Google to check the TXT and take ownership")
    vd.add_argument("domain", help="bare domain, NOT the sc-domain: form")
    q = sub.add_parser("query", help="search analytics: clicks/impressions over a window")
    q.add_argument("site")
    q.add_argument("--days", type=int_at_least(1), default=28,
                   help="window length; 0 would invert the window")
    q.add_argument("--lag", type=int_at_least(0), default=3,
                   help="end the window this many days back; the API's last ~2 days are incomplete")
    q.add_argument("--dimension", choices=["query", "page", "country", "device", "date"])
    q.add_argument("--limit", type=int_at_least(1), default=25)
    sm = sub.add_parser("sitemaps")
    sm.add_argument("site")
    ss = sub.add_parser("submit-sitemap")
    ss.add_argument("site")
    ss.add_argument("sitemap")
    sd = sub.add_parser("delete-sitemap")
    sd.add_argument("site")
    sd.add_argument("sitemap")
    ins = sub.add_parser("inspect")
    ins.add_argument("site")
    ins.add_argument("--sitemap")
    ins.add_argument("--limit", type=int, default=0)
    ins.add_argument("--out")
    a = p.parse_args()
    if a.cmd == "auth-url":
        print(auth_url())
    elif a.cmd == "auth":
        listen() if a.listen else exchange(a.redirect)
    elif a.cmd == "sites":
        cmd_sites(a)
    elif a.cmd == "scopes":
        cmd_scopes(a)
    elif a.cmd == "add-site":
        cmd_add_site(a)
    elif a.cmd == "verify-token":
        cmd_verify_token(a)
    elif a.cmd == "verify-domain":
        cmd_verify_domain(a)
    elif a.cmd == "query":
        cmd_query(a)
    elif a.cmd == "sitemaps":
        cmd_sitemaps(a)
    elif a.cmd == "submit-sitemap":
        cmd_submit(a)
    elif a.cmd == "delete-sitemap":
        cmd_delete(a)
    elif a.cmd == "inspect":
        cmd_inspect(a)


if __name__ == "__main__":
    main()
