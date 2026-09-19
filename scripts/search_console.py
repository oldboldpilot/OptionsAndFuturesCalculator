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
    python3 scripts/search_console.py inspect SITE [--sitemap URL] [--limit N] [--out FILE]

SITE is the Search Console property exactly as `sites` lists it, e.g.
"sc-domain:mortgagefvcalculator.com" or "https://mortgagefvcalculator.com/".

Nothing here ever prints the client secret, an auth code, or a token.
"""
from __future__ import annotations

import argparse
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
SCOPE = "https://www.googleapis.com/auth/webmasters"
PORT = 8765
REDIRECT_URI = f"http://localhost:{PORT}/"
API = "https://www.googleapis.com/webmasters/v3"
INSPECT_API = "https://searchconsole.googleapis.com/v1/urlInspection/index:inspect"


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
    tok["obtained_at"] = int(time.time())
    write_private(TOKEN_PATH, tok)
    os.remove(STATE_PATH)
    print(f"ok: token saved to {os.path.relpath(TOKEN_PATH, ROOT)} (0600, gitignored)")


def listen() -> None:
    url = auth_url()
    print("Open this link in a browser ON THIS MACHINE and approve access:\n")
    print(url + "\n")
    print(f"Waiting on {REDIRECT_URI} for Google's redirect (5 minutes)...")
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
    srv.timeout = 300
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


def sitemap_urls(url: str) -> list[str]:
    with urllib.request.urlopen(url, timeout=60) as r:
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
    sm = sub.add_parser("sitemaps")
    sm.add_argument("site")
    ss = sub.add_parser("submit-sitemap")
    ss.add_argument("site")
    ss.add_argument("sitemap")
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
    elif a.cmd == "sitemaps":
        cmd_sitemaps(a)
    elif a.cmd == "submit-sitemap":
        cmd_submit(a)
    elif a.cmd == "inspect":
        cmd_inspect(a)


if __name__ == "__main__":
    main()
