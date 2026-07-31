# Securing the financial API

A threat model and design for authenticating customers of
`sensen.finance.Finance` and `calculator.OptionsCalculator`, given that some
customers embed the API in their own web pages.

---

## 1. The constraint everything follows from

**A key in an embeddable widget is public.** It ships inside the customer's
HTML to every visitor of their site. So its security cannot come from secrecy —
anyone who views source has it.

This is not a flaw to engineer around; it is what embedding means. The same is
true of our own frontend, a static Cloudflare Pages export.

What a public key CAN do is **name the caller**. Once a request is attributable,
three things become possible that are impossible today: restrict where it works,
ration it, and switch it off. That is the whole design.

Two key types, following the model Stripe uses for exactly this problem:

| | Publishable `pk_live_…` | Secret `sk_live_…` |
| --- | --- | --- |
| Where it lives | Customer's web page, visible | Customer's server, never a browser |
| Protected by | Origin allowlist + quota + revocation | Secrecy + quota + revocation |
| May call | Compute RPCs only | Everything |
| If leaked | Usable only from the registered origins in a browser; attributable and revocable anywhere | Full access as that customer until revoked |

A secret key arriving **with** a browser `Origin` header is treated as leaked:
refused and logged loudly. There is no legitimate reason for one to be there.

---

## 2. What was found

Evidence gathered from the running system and the source, not from a checklist.

### Verified good

- **gRPC reflection is not registered.** The schema is not self-describing, so
  an attacker must obtain the `.proto` or reverse the bundle rather than ask the
  server what it offers.
- **The API key is never logged.** No log statement in `quota.cpp` touches it.
- **Secrets never reach the repository.** The `.env` is held on the NAS and
  gitignored. Worth stating explicitly because leaked-credentials-in-git is the
  single most common way API keys escape, and it is closed here by practice
  rather than left to discipline at commit time.
- **Input validation is genuinely strict.** Decimals are validated before
  parsing (`BigDecimal::parse` skips non-digits, so `"12x3"` would otherwise
  become `123`), terms are capped at 1200 months, ragged batches are refused,
  and underspecified bonds are rejected rather than guessed at.
- **Compute quotas exist and are priced by real work**, so an expensive call
  cannot hide behind a request count.

### Gaps

**H1 — No authentication on any RPC.** Neither service returns
`UNAUTHENTICATED` or `PERMISSION_DENIED` anywhere. The API key selects a quota
tier and grants nothing; an absent key is anonymous, not refused. Anyone with
the URL has the same access as any customer.

**M4 — Keys are plaintext at rest and compared non-constant-time.**
`QUOTA_API_KEYS` holds the keys themselves, and lookup is
`key_to_tier_.find(key)` — an ordinary hash-map probe whose timing varies with
the input.

Downgraded from High after checking how secrets are actually handled here: the
`.env` lives on the NAS and is gitignored, so the repository-leak vector — by
far the most common way API keys escape — is already closed by practice, not by
hope. That is the right control and it is working.

Hashing addresses a different and much narrower set of vectors that remain:
`/proc/self/environ` inside the container, a crash dump, and the Railway
dashboard, where variables are readable by anyone with account access. It is
worth doing anyway because it costs nothing and changes a property rather than
adding a control: with SHA-256 at rest the stored form **is not a credential**,
so none of those vectors yield a working key even in principle. Constant-time
comparison closes the timing oracle in the same change.

**M1 — No message size cap.** Neither `SetMaxReceiveMessageSize` in the engine
nor a body limit in Envoy. gRPC's implicit 4 MB default is the only thing
standing between the service and a memory-exhaustion attempt — and nothing in
the repository states that, so a future change could raise it without anyone
noticing. This matters more than it looks: **the quota guard runs after
deserialization**, so by the time a call is priced its message is already
resident. Size limits have to sit in front of the parser, not behind it.

**M2 — CORS allows every origin.** `regex: ".*"`, with the requesting origin
echoed back. Correct for a deliberately open API; wrong the moment keys exist,
because it removes the browser's own enforcement of where a publishable key may
be used.

**M3 — No audit trail.** There is no record of who called what. The question
"is someone already using this?" cannot be answered — only guessed at.

**L1 — No failed-authentication rate limit.** Once keys are required, an
attacker can probe them. With 256-bit keys brute force is infeasible, but
unlimited failed attempts are still free reconnaissance and should be capped per
source.

### Explicitly not a finding

- **Plaintext gRPC on :50051.** `InsecureServerCredentials`, but the listener is
  container-internal: TLS terminates at Railway's edge and Envoy forwards over
  loopback. Adding TLS on that hop would encrypt a wire that never leaves the
  container.
- **Data exposure.** There is none to expose. Every RPC is a pure calculation
  over inputs the caller supplies; none reads stored data or touches Postgres.
  This is why the design below is about *access and cost*, not confidentiality —
  and it would have to be revisited the moment an RPC returns stored data.

---

## 3. What is and is not achievable

Stated plainly, because a security design that overpromises is worse than none.

| Goal | Achievable | Mechanism |
| --- | --- | --- |
| Know who is calling | **Yes** | Required key + audit log |
| Stop another *website* embedding our API | **Yes** | Per-key origin allowlist; a browser cannot forge `Origin` |
| Ration and bill per customer | **Yes** | Quota keyed on the validated key |
| Cut off a specific customer | **Yes** | Revocation |
| Stop a leaked publishable key being replayed from a script | **No** | It is public; a script can send any `Origin`. Detect, ration, revoke. |
| Keep the schema secret | **No** | It is in the customer's bundle |

The honest summary: this design makes unauthorised use **attributable, limited
and revocable**. It does not make it impossible, and for an API that runs in a
browser nothing can.

---

## 4. Design

### 4.1 Key format

```
pk_live_<43 chars base64url>      publishable, 256-bit entropy
sk_live_<43 chars base64url>      secret
```

The prefix is load-bearing: it lets the service reject a secret key seen in a
browser, and lets a customer tell at a glance which one they are pasting. Secret
scanners (GitHub, GitLab) also key on prefixes, so a leaked `sk_live_` is more
likely to be caught by someone else's tooling.

Stored as `sha256(key)`. The plaintext exists only at issuance, in the
customer's hands, and in the request being checked.

### 4.2 Validation order

Cheapest and most decisive first, so a bad request is refused before it costs
anything:

```
1  message size           before deserialization    (transport)
2  key present            no key -> UNAUTHENTICATED
3  key format + prefix    reject malformed with no lookup at all
4  hash + constant-time lookup
5  enabled? not expired?  -> UNAUTHENTICATED
6  publishable + Origin present -> origin allowlist  -> PERMISSION_DENIED
   secret + Origin present      -> leaked            -> PERMISSION_DENIED + alert
7  scope: may this key call this service/method?     -> PERMISSION_DENIED
8  quota: rate + compute budget                      -> RESOURCE_EXHAUSTED
9  audit log: key id, method, origin, decision
```

Step 3 is what keeps step 4 cheap under a probing attack: a malformed key never
reaches the hash.

### 4.3 Origin binding

Each key carries an allowlist:

```json
"sha256:9f86d081…": {
  "id": "acme-risk",
  "type": "publishable",
  "tier": "partner",
  "origins": ["https://acme.example", "https://*.acme.example"],
  "expires": "2027-01-01"
}
```

A browser sets `Origin` on cross-origin requests and **cannot be made to lie**
about it by the page it is on. So a publishable key pasted into a different
site's HTML stops working there. A non-browser caller can send anything, which
is precisely why publishable keys are restricted to compute RPCs and capped by
quota.

An empty `origins` list means no browser use — appropriate for secret keys.

### 4.4 CORS follows the key

Envoy's blanket `.*` is replaced by the union of registered origins. The browser
then refuses the response before the page can read it, which is a second,
independent layer over the engine's own check.

### 4.5 What our own frontend uses

Its own publishable key, origin-locked to `optionsandfuturescalculator.com` and
`*.pages.dev`. It is not a secret — it is visible in the bundle — and that is
fine. Its purpose is to separate our traffic from everyone else's, which is what
makes unfamiliar traffic *visible*.

### 4.6 Audit

One structured line per call:

```
key=acme-risk type=publishable method=ComputeAmortization origin=https://acme.example cu=31 decision=allow
key=<none>    type=-           method=PriceOptionMonteCarlo origin=-  cu=1001 decision=deny:unauthenticated
```

The key itself is never written. `key=` carries the human label, so a log leak
does not become a credential leak.

---

## 5. Rollout

A change that starts refusing traffic must not be switched on blind.

1. **Observe.** Keys optional, everything logged. Answers "is anyone already
   using this, and from where?" with data instead of a guess.
2. **Warn.** Unkeyed calls still served, at the anonymous tier, logged as
   `would-deny`. Confirms nothing legitimate is about to break.
3. **Enforce.** `FINANCE_REQUIRE_KEY=1`. Unkeyed calls refused.

The calculator stays public throughout — it serves the website, and a static
export cannot hold a secret. It gets attribution via its own publishable key,
not a gate.

---

## 6. Deliberately not doing

- **Obfuscating the frontend bundle.** One debugger session undoes it. It buys
  nothing and creates the impression of protection, which is worse than none.
- **IP allowlists as a primary control.** Customer egress IPs change; this
  breaks integrations for no security gain against an attacker who can rent an
  address.
- **mTLS.** Genuinely stronger for server-to-server, and unusable for the
  embedded browser case that motivated this. Worth revisiting for a large
  server-side customer, as an option alongside secret keys rather than instead
  of them.
- **Cloudflare WAF / bot management.** Would be a strong extra layer, but
  `api.optionsandfuturescalculator.com` is deliberately **un-proxied** —
  `CLAUDE.md` records that proxying broke Railway's ownership verification and
  the ACME challenge, and took the endpoint down. Revisit as its own piece of
  work with the TLS story re-solved, not as a side effect of this one.
