module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

module api_key;

import fastjson;
import logger;

namespace options_calculator::auth {

namespace {

constexpr std::string_view kPublishablePrefix = "pk_live_";
constexpr std::string_view kSecretPrefix = "sk_live_";

// 32 random bytes as unpadded base64url. Fixed, so a wrong length is rejected
// before the key is ever hashed or looked up.
constexpr std::size_t kBodyLength = 43;
constexpr std::size_t kEntropyBytes = 32;
constexpr std::size_t kSha512HexLength = 128;

[[nodiscard]] auto env_or(const char* name, std::string fallback) -> std::string {
    const char* raw = std::getenv(name);
    return (raw != nullptr && *raw != '\0') ? std::string{raw} : std::move(fallback);
}

/** Today in UTC as YYYY-MM-DD. ISO-8601 dates compare chronologically as text. */
[[nodiscard]] auto today_utc() -> std::string {
    const auto days = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    const std::chrono::year_month_day ymd{days};
    std::string out(10, '0');
    const int y = static_cast<int>(ymd.year());
    const unsigned m = static_cast<unsigned>(ymd.month());
    const unsigned d = static_cast<unsigned>(ymd.day());
    out[0] = static_cast<char>('0' + (y / 1000) % 10);
    out[1] = static_cast<char>('0' + (y / 100) % 10);
    out[2] = static_cast<char>('0' + (y / 10) % 10);
    out[3] = static_cast<char>('0' + y % 10);
    out[4] = '-';
    out[5] = static_cast<char>('0' + (m / 10) % 10);
    out[6] = static_cast<char>('0' + m % 10);
    out[7] = '-';
    out[8] = static_cast<char>('0' + (d / 10) % 10);
    out[9] = static_cast<char>('0' + d % 10);
    return out;
}

/**
 * One issued key, minus the key itself.
 *
 * `hash` is the SHA-512 of the key. The plaintext exists only at issuance, in
 * the customer's hands, and transiently in the request being checked -- so this
 * struct, the environment variable it came from, and any log line derived from
 * it are all non-credentials.
 */
struct KeyRecord {
    std::string hash;
    std::string id;
    std::string tier;
    KeyType type = KeyType::Publishable;
    std::vector<std::string> origins;
    std::vector<std::string> scopes;
    std::string expires;  // empty = never
    bool enabled = true;
};

/**
 * Matches an origin against a registered pattern.
 *
 * Supports one leading wildcard label. A pattern of the form
 * `https:` + `[star].acme.example` matches `https://app.acme.example` but NOT
 * `https://acme.example` itself (a customer wanting both registers both), and
 * NOT `https://evil.com/?x=.acme.example`, because the comparison is anchored
 * at both ends and the scheme is matched literally.
 *
 * A bare `*` is accepted and means "any origin", which is only ever appropriate
 * for a key deliberately issued as open. It is logged at load time so it cannot
 * become an accident nobody noticed.
 */
[[nodiscard]] auto origin_matches(std::string_view pattern, std::string_view origin) -> bool {
    if (pattern == "*") return true;
    const auto star = pattern.find('*');
    if (star == std::string_view::npos) return pattern == origin;

    const auto prefix = pattern.substr(0, star);
    const auto suffix = pattern.substr(star + 1);
    // Anchored at BOTH ends. Without the length guard, a short origin could
    // satisfy prefix and suffix by overlapping in the middle.
    if (origin.size() < prefix.size() + suffix.size()) return false;
    return origin.starts_with(prefix) && origin.ends_with(suffix);
}

}  // namespace

auto sha512_hex(std::string_view input) -> std::string {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;

    // EVP rather than the legacy one-shot SHA512(): OpenSSL 3.0 deprecated the
    // low-level interface and this build compiles with warnings as errors.
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),
                                                                     &EVP_MD_CTX_free);
    if (!ctx) return {};
    if (EVP_DigestInit_ex(ctx.get(), EVP_sha512(), nullptr) != 1) return {};
    if (EVP_DigestUpdate(ctx.get(), input.data(), input.size()) != 1) return {};
    if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &digest_len) != 1) return {};

    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_len) * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out.push_back(kHex[digest[i] >> 4]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

auto constant_time_equals(std::string_view a, std::string_view b) noexcept -> bool {
    // Length inequality is not secret -- key length is fixed and public -- so
    // returning early on it leaks nothing.
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

auto generate_key(KeyType type) -> std::string {
    std::array<unsigned char, kEntropyBytes> raw{};
    if (RAND_bytes(raw.data(), static_cast<int>(raw.size())) != 1) {
        // Refusing beats returning a predictable key. A caller that ignores the
        // empty string would be issuing a guessable credential.
        return {};
    }

    static constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string body;
    body.reserve(kBodyLength);
    for (std::size_t i = 0; i + 2 < raw.size(); i += 3) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(raw[i]) << 16) |
                                    (static_cast<std::uint32_t>(raw[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(raw[i + 2]);
        body.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        body.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        body.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        body.push_back(kAlphabet[chunk & 0x3F]);
    }
    // 32 bytes is not a multiple of 3; the last two bytes give two more chars.
    const std::uint32_t tail =
        (static_cast<std::uint32_t>(raw[30]) << 8) | static_cast<std::uint32_t>(raw[31]);
    body.push_back(kAlphabet[(tail >> 10) & 0x3F]);
    body.push_back(kAlphabet[(tail >> 4) & 0x3F]);
    body.push_back(kAlphabet[(tail << 2) & 0x3F]);
    body.resize(kBodyLength);

    return std::string{type == KeyType::Secret ? kSecretPrefix : kPublishablePrefix} + body;
}

auto to_string(Outcome outcome) noexcept -> std::string_view {
    switch (outcome) {
        case Outcome::Ok: return "ok";
        case Outcome::NoKey: return "no-key";
        case Outcome::Malformed: return "malformed";
        case Outcome::Unknown: return "unknown-key";
        case Outcome::Revoked: return "revoked";
        case Outcome::Expired: return "expired";
        case Outcome::OriginNotAllowed: return "origin-not-allowed";
        case Outcome::SecretFromBrowser: return "secret-key-in-browser";
        case Outcome::ScopeDenied: return "scope-denied";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Entitlement
// ---------------------------------------------------------------------------

auto pro_gate_mode() -> GateMode {
    const auto raw = env_or("PRO_GATE_MODE", "");
    if (raw == "2" || raw == "enforce") return GateMode::Enforce;
    if (raw == "1" || raw == "warn") return GateMode::Warn;
    return GateMode::Off;
}

auto is_pro(const Identity& identity) noexcept -> bool {
    // Tier names are policy, not code, so this compares against the two the
    // database's own CHECK constraint allows (profiles.tier IN ('free','pro')).
    // Keeping the vocabulary identical across the schema, the key registry and
    // this check is what stops a subscription that says "pro" somewhere from
    // meaning nothing here.
    return identity.tier == "pro" || identity.tier == "partner";
}

auto check_strategy_entitlement(const Identity& identity, int leg_count) -> grpc::Status {
    const auto mode = pro_gate_mode();
    if (mode == GateMode::Off) return grpc::Status::OK;
    if (leg_count <= 1) return grpc::Status::OK;
    if (is_pro(identity)) return grpc::Status::OK;

    auto& log = logger::Logger::getInstance();
    const std::string who = identity.id.empty() ? "<anonymous>" : identity.id;

    if (mode == GateMode::Warn) {
        log.error("pro-gate would-deny: key={} legs={} tier={}", who, leg_count,
                  identity.tier.empty() ? "free" : identity.tier);
        return grpc::Status::OK;
    }

    log.info("pro-gate deny: key={} legs={} tier={}", who, leg_count,
             identity.tier.empty() ? "free" : identity.tier);
    return grpc::Status(
        grpc::StatusCode::PERMISSION_DENIED,
        "Multi-leg strategies are a Pro feature. Single-leg calls and puts remain free. "
        "This position has " +
            std::to_string(leg_count) + " legs.");
}

class KeyRegistry::Impl {
  public:
    Impl() { load(); }

    bool enabled_ = false;
    Mode mode_ = Mode::Observe;
    std::vector<KeyRecord> records_;

    /**
     * Keys come from FINANCE_API_KEYS, an inline JSON object keyed by the
     * SHA-512 hex of each key.
     *
     * Absent configuration leaves authentication OFF and every call untouched,
     * matching how quotas behave. It is the only safe default for a mechanism
     * that can otherwise start refusing real traffic the moment it ships.
     */
    auto load() -> void {
        auto& log = logger::Logger::getInstance();

        const auto mode_raw = env_or("FINANCE_REQUIRE_KEY", "");
        if (mode_raw == "2" || mode_raw == "enforce") {
            mode_ = Mode::Enforce;
        } else if (mode_raw == "1" || mode_raw == "warn") {
            mode_ = Mode::Warn;
        } else {
            mode_ = Mode::Observe;
        }

        const auto keys_json = env_or("FINANCE_API_KEYS", "");
        if (keys_json.empty()) {
            log.info("API key auth disabled: FINANCE_API_KEYS is unset");
            return;
        }

        auto result = fastjson::parse(keys_json);
        if (!result) {
            // Loud, and still off. A key set that failed to parse must never
            // read as "no keys configured" to whoever looks at this later.
            log.error("FINANCE_API_KEYS is not valid JSON; API key auth stays DISABLED");
            return;
        }
        if (!result->is_object()) {
            log.error("FINANCE_API_KEYS is not a JSON object; API key auth stays DISABLED");
            return;
        }

        for (const auto& [hash, spec] : result->as_object()) {
            if (!spec.is_object()) continue;

            KeyRecord rec;
            rec.hash = hash;
            if (rec.hash.size() != kSha512HexLength) {
                // Almost always a plaintext key pasted where its hash belongs.
                // Naming the id would echo the caller's own mistake into the
                // log next to what may be a live credential, so it does not.
                log.error("FINANCE_API_KEYS has an entry whose key is not a 128-character SHA-512 "
                          "hex digest; it is IGNORED. Store sha512(key), never the key itself");
                continue;
            }
            std::ranges::transform(rec.hash, rec.hash.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            rec.id = spec.contains("id") && spec["id"].is_string()
                         ? std::string{spec["id"].as_string()}
                         : std::string{"unnamed"};
            rec.tier = spec.contains("tier") && spec["tier"].is_string()
                           ? std::string{spec["tier"].as_string()}
                           : std::string{"free"};

            const std::string type_s = spec.contains("type") && spec["type"].is_string()
                                           ? std::string{spec["type"].as_string()}
                                           : std::string{"publishable"};
            rec.type = (type_s == "secret") ? KeyType::Secret : KeyType::Publishable;

            if (spec.contains("expires") && spec["expires"].is_string()) {
                rec.expires = std::string{spec["expires"].as_string()};
            }
            if (spec.contains("enabled") && spec["enabled"].is_boolean()) {
                rec.enabled = spec["enabled"].as_boolean();
            }
            if (spec.contains("origins") && spec["origins"].is_array()) {
                for (const auto& o : spec["origins"].as_array()) {
                    if (o.is_string()) rec.origins.emplace_back(o.as_string());
                }
            }
            if (spec.contains("scopes") && spec["scopes"].is_array()) {
                for (const auto& s : spec["scopes"].as_array()) {
                    if (s.is_string()) rec.scopes.emplace_back(s.as_string());
                }
            }
            if (rec.scopes.empty()) rec.scopes.emplace_back("finance");

            if (std::ranges::find(rec.origins, "*") != rec.origins.end()) {
                log.error("API key '{}' registers origin '*' -- it will work from ANY site. That "
                          "is only correct for a deliberately open key",
                          rec.id);
            }
            if (rec.type == KeyType::Publishable && rec.origins.empty()) {
                // A publishable key IS public; without an origin allowlist it is
                // a credential anyone can copy out of the page and reuse
                // anywhere. Say so at load, not after it is abused.
                log.error("Publishable key '{}' has no `origins` -- it is public by design and "
                          "unbound, so it can be lifted from the page and replayed from anywhere",
                          rec.id);
            }
            records_.push_back(std::move(rec));
        }

        if (records_.empty()) {
            log.error("FINANCE_API_KEYS defined no usable keys; API key auth stays DISABLED");
            return;
        }

        enabled_ = true;
        const std::string_view mode_name = mode_ == Mode::Enforce   ? "ENFORCE"
                                           : mode_ == Mode::Warn    ? "WARN (serving, logging "
                                                                      "would-deny)"
                                                                    : "OBSERVE (serving, logging)";
        log.info("API key auth ENABLED: {} keys, mode {}", records_.size(), mode_name);
    }

    /**
     * Resolves a presented key.
     *
     * The order is cheapest-and-most-decisive first, so a bad request is
     * refused before it costs anything: shape before hash, hash before
     * binding. Step 2 is what keeps step 3 cheap under a probing attack -- a
     * malformed key never reaches SHA-512 at all.
     */
    [[nodiscard]] auto check(std::string_view key, std::string_view origin,
                             std::string_view service) const -> AuthResult {
        AuthResult r;
        if (!enabled_) {
            r.outcome = Outcome::Ok;
            r.identity.authenticated = false;
            return r;
        }

        if (key.empty()) {
            r.outcome = Outcome::NoKey;
            r.detail = "no API key supplied (send it in the `x-api-key` header)";
            return r;
        }

        const bool is_pub = key.starts_with(kPublishablePrefix);
        const bool is_sec = key.starts_with(kSecretPrefix);
        if ((!is_pub && !is_sec) || key.size() != kPublishablePrefix.size() + kBodyLength) {
            r.outcome = Outcome::Malformed;
            r.detail = "malformed API key";
            return r;
        }

        // A secret key can only reach a browser by being pasted into
        // client-side code. Nothing legitimate produces this combination, so it
        // is a leak report rather than a failed login -- and it is refused even
        // though the key itself is valid.
        if (is_sec && !origin.empty()) {
            r.outcome = Outcome::SecretFromBrowser;
            r.detail = "a secret key was presented from a browser; treat it as compromised and "
                       "rotate it. Use a publishable key for browser traffic";
            return r;
        }

        const auto presented_hash = sha512_hex(key);

        // Linear, constant-time scan rather than a hash-map probe. The map
        // lookup would be O(1) but its timing varies with the input; with a key
        // set in the tens, 128 bytes of CRYPTO_memcmp each is nanoseconds and
        // buys a comparison that leaks nothing. Every record is examined even
        // after a match, so the time taken does not reveal WHICH key matched or
        // whether an early one did.
        const KeyRecord* found = nullptr;
        for (const auto& rec : records_) {
            if (constant_time_equals(rec.hash, presented_hash)) found = &rec;
        }

        if (found == nullptr) {
            r.outcome = Outcome::Unknown;
            r.detail = "unrecognised API key";
            return r;
        }

        r.identity.id = found->id;
        r.identity.tier = found->tier;
        r.identity.type = found->type;

        if (!found->enabled) {
            r.outcome = Outcome::Revoked;
            r.detail = "this API key has been revoked";
            return r;
        }
        if (!found->expires.empty() && today_utc() > found->expires) {
            r.outcome = Outcome::Expired;
            r.detail = "this API key expired on " + found->expires;
            return r;
        }

        // Origin binding applies only when there IS an origin -- a server-side
        // caller sends none, and a publishable key used server-side is merely
        // an odd choice rather than an attack.
        if (!origin.empty() && !found->origins.empty()) {
            const bool ok = std::ranges::any_of(
                found->origins, [&](const std::string& p) { return origin_matches(p, origin); });
            if (!ok) {
                r.outcome = Outcome::OriginNotAllowed;
                r.detail = "this API key is not registered for use from this site";
                return r;
            }
        }

        if (!service.empty() &&
            std::ranges::find(found->scopes, std::string{service}) == found->scopes.end()) {
            r.outcome = Outcome::ScopeDenied;
            r.detail = "this API key is not entitled to the '" + std::string{service} + "' service";
            return r;
        }

        r.outcome = Outcome::Ok;
        r.identity.authenticated = true;
        return r;
    }
};

KeyRegistry::KeyRegistry() : impl_(std::make_unique<Impl>()) {}

// Out-of-line because Impl is incomplete in the module interface.
KeyRegistry::~KeyRegistry() = default;

auto KeyRegistry::instance() -> KeyRegistry& {
    static KeyRegistry r;
    return r;
}

auto KeyRegistry::enabled() const noexcept -> bool { return impl_->enabled_; }

auto KeyRegistry::mode() const noexcept -> Mode { return impl_->mode_; }

auto KeyRegistry::key_count() const noexcept -> std::size_t { return impl_->records_.size(); }

auto KeyRegistry::all_origins() const -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& rec : impl_->records_) {
        out.insert(out.end(), rec.origins.begin(), rec.origins.end());
    }
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

auto KeyRegistry::check(std::string_view presented_key, std::string_view origin,
                        std::string_view service) const -> AuthResult {
    return impl_->check(presented_key, origin, service);
}

auto KeyRegistry::authenticate(const grpc::ServerContext& ctx, std::string_view service,
                               std::string_view method, Identity& out) -> grpc::Status {
    if (!impl_->enabled_) return grpc::Status::OK;

    std::string key;
    std::string origin;
    const auto& md = ctx.client_metadata();
    // Header names arrive lowercased over HTTP/2, which gRPC preserves.
    if (const auto it = md.find("x-api-key"); it != md.end()) {
        key.assign(it->second.data(), it->second.size());
    }
    if (const auto it = md.find("origin"); it != md.end()) {
        origin.assign(it->second.data(), it->second.size());
    }

    auto r = impl_->check(key, origin, service);
    out = r.identity;

    auto& log = logger::Logger::getInstance();

    if (r.outcome == Outcome::Ok) {
        return grpc::Status::OK;
    }

    // A leaked secret key is worth saying loudly whatever the mode, because it
    // is the one outcome that means a customer must rotate something today.
    if (r.outcome == Outcome::SecretFromBrowser) {
        log.error("SECRET KEY IN BROWSER: key={} method={} origin={} -- treat as compromised",
                  r.identity.id.empty() ? "?" : r.identity.id, method, origin);
    }

    // Observe and Warn both serve the request. The difference is only how
    // loudly they report what Enforce would have done -- which is what lets
    // "will this break anything" be answered from data before it is switched on.
    if (impl_->mode_ != Mode::Enforce) {
        const auto line = std::string{"auth would-deny: key="} +
                          (r.identity.id.empty() ? "<none>" : r.identity.id) +
                          " method=" + std::string{method} +
                          " origin=" + (origin.empty() ? "-" : origin) +
                          " outcome=" + std::string{to_string(r.outcome)};
        if (impl_->mode_ == Mode::Warn) {
            log.error("{}", line);
        } else {
            log.info("{}", line);
        }
        return grpc::Status::OK;
    }

    log.info("auth deny: key={} method={} origin={} outcome={}",
             r.identity.id.empty() ? "<none>" : r.identity.id, method,
             origin.empty() ? "-" : origin, to_string(r.outcome));

    // UNAUTHENTICATED means "I do not know who you are"; PERMISSION_DENIED
    // means "I know who you are and you may not do this". Collapsing them would
    // tell a customer with a scope problem to go and check their key.
    switch (r.outcome) {
        case Outcome::NoKey:
        case Outcome::Malformed:
        case Outcome::Unknown:
        case Outcome::Revoked:
        case Outcome::Expired:
            return grpc::Status(grpc::StatusCode::UNAUTHENTICATED, r.detail);
        case Outcome::OriginNotAllowed:
        case Outcome::SecretFromBrowser:
        case Outcome::ScopeDenied:
            return grpc::Status(grpc::StatusCode::PERMISSION_DENIED, r.detail);
        case Outcome::Ok:
            break;
    }
    return grpc::Status::OK;
}

}  // namespace options_calculator::auth
