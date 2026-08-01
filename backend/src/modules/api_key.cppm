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

/** Human-readable name for an outcome, for logs. */
[[nodiscard]] auto to_string(Outcome outcome) noexcept -> std::string_view;

}  // namespace options_calculator::auth
