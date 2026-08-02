module;
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <grpcpp/grpcpp.h>

export module api_key;

export namespace options_calculator::auth {

/**
 * Per-customer API key authentication for the public gRPC surface.
 *
 * The governing constraint, from docs/API_SECURITY.md: a key that ships inside
 * an embeddable widget is PUBLIC. It is in the customer's HTML, visible to
 * every visitor of their site. Its security therefore cannot come from secrecy
 * -- it comes from BINDING. A publishable key names its caller, works only from
 * the origins that caller registered, is rationed, and can be switched off.
 *
 * This module is deliberately separate from `quota`. Quotas answer "how much
 * may this caller use"; authentication answers "who is this, and may they call
 * at all". Keeping them apart means an unrecognised key can be a refusal here
 * without quota having to become an authentication system it was not designed
 * to be -- the exact conflation the quota module documents itself as avoiding.
 */

/**
 * Publishable keys live in browsers; secret keys must never.
 *
 * The distinction is load-bearing rather than cosmetic: a `sk_` key arriving
 * with a browser `Origin` header can only have got there by being pasted into
 * client-side code, so that combination is treated as a leak rather than as a
 * successful authentication.
 */
enum class KeyType : std::uint8_t { Publishable, Secret };

/** Why a request was (or would have been) refused. `Ok` is the only pass. */
enum class Outcome : std::uint8_t {
    Ok,
    NoKey,              // no x-api-key at all
    Malformed,          // wrong prefix or wrong length -- rejected before hashing
    Unknown,            // well-formed, but not issued by us
    Revoked,            // issued, then disabled
    Expired,            // past its expiry date
    OriginNotAllowed,   // publishable key used from an unregistered site
    SecretFromBrowser,  // secret key seen with an Origin header -- treat as leaked
    ScopeDenied,        // valid key, but not entitled to this service
};

/**
 * How strictly to act on the outcome.
 *
 * A change that starts refusing traffic must not be switched on blind, so the
 * rollout is staged. Observe and Warn both SERVE the request; they differ only
 * in how loudly they say what Enforce would have done. This is what makes it
 * possible to answer "is anything legitimate about to break" with data rather
 * than with a guess.
 */
enum class Mode : std::uint8_t { Observe, Warn, Enforce };

/** The caller, once resolved. Every field here is safe to write to a log. */
struct Identity {
    std::string id;                        // human label, e.g. "acme-risk"
    std::string tier;                      // selects quota limits
    KeyType type = KeyType::Publishable;
    bool authenticated = false;

    /**
     * Limits carried by the KEY itself, overriding whatever its tier says.
     *
     * A tier is the right unit when many customers share a shape ("free",
     * "business"). It is the wrong unit for the case this API is actually being
     * handed off for: one customer who negotiated one number. Expressing that
     * through tiers means inventing a tier per customer in QUOTA_POLICY and
     * hoping the name stays in agreement with the one in FINANCE_API_KEYS --
     * two variables, no cross-check, and a typo silently downgrades them to the
     * anonymous allowance.
     *
     * `has_limits` distinguishes "not specified" from "specified as zero",
     * because zero already means UNLIMITED on both axes. Without the flag, a
     * key that omitted its limits would be indistinguishable from one granted
     * unlimited access -- the most expensive possible way to be wrong.
     */
    bool has_limits = false;
    std::int64_t requests_per_minute = 0;
    double compute_units_per_hour = 0.0;
};

/** The outcome of one authentication attempt. */
struct AuthResult {
    Outcome outcome = Outcome::NoKey;
    Identity identity;
    std::string detail;   // human-readable, safe to log and to return
    bool denied = false;  // whether this was ACTUALLY refused, given the mode
};

/**
 * The issued-key registry.
 *
 * A singleton because it is immutable configuration read once at startup: a
 * second copy would be identical, and re-parsing per request would put JSON
 * parsing on the hot path of every call.
 */
class KeyRegistry {
  public:
    [[nodiscard]] static auto instance() -> KeyRegistry&;

    /**
     * Authenticates a call from its gRPC context.
     *
     * Reads `x-api-key` and `origin` from client metadata. Returns OK to
     * proceed, or UNAUTHENTICATED / PERMISSION_DENIED with a message that says
     * what is wrong without saying anything that helps an attacker refine a
     * guess.
     */
    [[nodiscard]] auto authenticate(const grpc::ServerContext& ctx, std::string_view service,
                                    std::string_view method, Identity& out) -> grpc::Status;

    /** The same check without gRPC types, for tests and the smoke gate. */
    [[nodiscard]] auto check(std::string_view presented_key, std::string_view origin,
                             std::string_view service) const -> AuthResult;

    /** Whether any keys are configured at all. False leaves every call untouched. */
    [[nodiscard]] auto enabled() const noexcept -> bool;

    /** Observe / Warn / Enforce. */
    [[nodiscard]] auto mode() const noexcept -> Mode;

    /** Number of keys loaded. For the startup log and diagnostics. */
    [[nodiscard]] auto key_count() const noexcept -> std::size_t;

    /** Every registered origin, for narrowing CORS. Sorted, de-duplicated. */
    [[nodiscard]] auto all_origins() const -> std::vector<std::string>;

    KeyRegistry(const KeyRegistry&) = delete;
    auto operator=(const KeyRegistry&) -> KeyRegistry& = delete;
    KeyRegistry(KeyRegistry&&) = delete;
    auto operator=(KeyRegistry&&) -> KeyRegistry& = delete;
    ~KeyRegistry();

  private:
    KeyRegistry();
    class Impl;
    // Out-of-line destructor: Impl is incomplete here and unique_ptr needs a
    // complete type to destroy it.
    std::unique_ptr<Impl> impl_;
};

/**
 * SHA-512 of `input`, lowercase hex (128 characters).
 *
 * SHA-512 rather than SHA-256 for margin and, unusually, for speed: it works on
 * 64-bit words in 1024-bit blocks, so on any 64-bit CPU it hashes faster per
 * byte than SHA-256 does. Grover's algorithm -- the only quantum attack that
 * applies to a preimage -- gives a quadratic speedup, taking SHA-512 from 2^512
 * to 2^256, which stays far outside reach.
 *
 * Deliberately NOT a slow KDF (Argon2/bcrypt/scrypt). Those exist to make each
 * guess expensive against LOW-entropy human passwords. These keys carry 256
 * bits of random entropy, so brute force is already infeasible and a work
 * factor buys nothing -- while costing tens of milliseconds and hundreds of
 * megabytes per verification on the request path, which would be a
 * denial-of-service vector of our own making.
 */
[[nodiscard]] auto sha512_hex(std::string_view input) -> std::string;

/**
 * Compares two equal-length strings in time independent of their contents.
 *
 * `==` and `memcmp` return as soon as they find a difference, so how long they
 * take reveals how many leading bytes matched -- enough, over many attempts, to
 * recover a secret byte by byte.
 */
[[nodiscard]] auto constant_time_equals(std::string_view a, std::string_view b) noexcept -> bool;

/** Generates a new key, `pk_live_`/`sk_live_` + 256 bits of CSPRNG base64url. */
[[nodiscard]] auto generate_key(KeyType type) -> std::string;

/**
 * HMAC-SHA512 of `message` under `secret`, truncated to 256 bits, base64url.
 *
 * Truncation is deliberate and safe: HMAC's security is bounded by the
 * narrower of the key and the output, and 256 bits is far beyond forgeable.
 * It halves the token length, which matters when a human has to paste it.
 */
[[nodiscard]] auto hmac_sha512_b64(std::string_view secret, std::string_view message)
    -> std::string;

/**
 * Verifies a signed subscription licence and fills `out` on success.
 *
 * A licence is `lk_live_<payload>.<signature>`, where payload is base64url JSON
 * carrying the Stripe customer, the tier and an expiry. It is SIGNED rather
 * than stored, which is what keeps the engine stateless: there is no table of
 * issued licences to query, cache or invalidate, and a licence can be minted by
 * the billing webhook without the engine being told about it.
 *
 * The trade is that revocation is by expiry rather than immediate. That suits
 * subscriptions -- the licence is issued to the end of the paid period and
 * reissued on renewal, so a cancellation stops the next issuance rather than
 * reaching back to kill the current one. A customer who has paid for the month
 * keeps the month, which is also what they are owed.
 *
 * Requires LICENCE_SIGNING_KEY. Without it no licence verifies, which is the
 * safe direction: an unset secret must not mean "accept anything".
 */
[[nodiscard]] auto verify_licence(std::string_view token, Identity& out) -> bool;

/**
 * Verifies a Supabase-issued access token (HS256) and fills `out` on success.
 *
 * Self-hosted Supabase signs its access tokens with a single shared
 * `JWT_SECRET` using HMAC-SHA256, so the engine can verify one without talking
 * to Supabase at all. That matters here: a per-request call to the auth server
 * would put a network hop on the hot path of every strategy calculation and
 * make the engine unavailable whenever Supabase is.
 *
 * The subscription tier is read from `app_metadata.tier`. That claim is chosen
 * because Supabase copies `app_metadata` into the access token automatically
 * and it is writable ONLY with the service role key -- a user cannot set their
 * own tier, whereas `user_metadata` is self-serve and would be a free upgrade
 * button. The billing webhook writes it; the browser never can.
 *
 * Requires SUPABASE_JWT_SECRET. Without it nothing verifies, which is the safe
 * direction: an unset secret must not read as "accept anything".
 */
[[nodiscard]] auto verify_supabase_jwt(std::string_view token, Identity& out) -> bool;

/** Human-readable name for an outcome, for logs. */
[[nodiscard]] auto to_string(Outcome outcome) noexcept -> std::string_view;

// ---------------------------------------------------------------------------
// Entitlement
//
// Which paid features an identity may use. It lives here rather than in its own
// module because `Identity` already carries the tier -- an entitlement is a
// question ABOUT an identity, and splitting the two would mean a module that
// exists to hold one predicate.
//
// The gate has to be SERVER-SIDE. The frontend is a static Cloudflare Pages
// export: anything it enforces is enforced by code the user already has on
// their machine, and the gRPC endpoint is reachable directly with curl. A
// client-side check is a label, not a lock.
// ---------------------------------------------------------------------------

/**
 * How strictly to apply the Pro gate.
 *
 * Off is the default and leaves every strategy free, exactly as before. The
 * staged rollout matters more here than it does for authentication: switching
 * this on turns a feature people already use into one they have to pay for, so
 * it must not happen as a side effect of a deploy.
 */
enum class GateMode : std::uint8_t { Off, Warn, Enforce };

/** Reads PRO_GATE_MODE. Unset means Off. */
[[nodiscard]] auto pro_gate_mode() -> GateMode;

/** Whether this identity carries a Pro entitlement. */
[[nodiscard]] auto is_pro(const Identity& identity) noexcept -> bool;

/**
 * Decides whether a strategy of `leg_count` legs may be computed.
 *
 * Single-leg positions -- a plain long call or put -- stay free. Anything the
 * caller had to combine (spreads, straddles, condors, butterflies) is the paid
 * feature. Returns OK when allowed, PERMISSION_DENIED with an upgrade message
 * when not, and always OK while the gate is Off or Warn.
 */
[[nodiscard]] auto check_strategy_entitlement(const Identity& identity, int leg_count)
    -> grpc::Status;

/**
 * Gates `calculator.assistant.StrategyAssistant/ParseStrategy` behind Pro,
 * unconditionally -- no free-tier carve-out the way `check_strategy_entitlement`
 * keeps single-leg calculator strategies free.
 *
 * The reason is cost asymmetry, not a judgment that natural-language parsing
 * is inherently premium. `cost_llm_generate` prices one call at roughly 8,845
 * compute units against `cost_default()`'s 1.0 -- comparable to a million-path
 * Monte Carlo -- and the call holds the ONE dedicated inference worker
 * exclusively for about 1.1s (`generate()` cannot run concurrently; see
 * assistant_service.cpp). Against the shared anonymous budget (120,000
 * compute-units/hour, `QUOTA_POLICY`), roughly 13-14 calls -- achievable in
 * well under a minute, nowhere near the 6000 req/min rate limit -- exhaust
 * the ENTIRE site's hourly compute allowance for every other anonymous caller
 * of the calculator and finance services. A single-leg calculator call costs
 * a handful of scalar ops and stays free for exactly that reason; this RPC
 * has no equivalently cheap case to protect.
 *
 * Same Off/Warn/Enforce semantics as `check_strategy_entitlement`: OK while
 * the gate is Off, OK-but-logged while Warn, PERMISSION_DENIED under Enforce
 * for a non-Pro identity.
 */
[[nodiscard]] auto check_assistant_entitlement(const Identity& identity) -> grpc::Status;

}  // namespace options_calculator::auth
