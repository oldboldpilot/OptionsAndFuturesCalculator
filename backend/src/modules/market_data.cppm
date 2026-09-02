module;
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

export module market_data;
import std;

import fastjson;
import logger;
import sgee.runtime.resilience;

/*
 * Market data.
 *
 * The provider is pluggable. Everything above the provider boundary — the
 * domain types, the caching, and the free functions the service calls — is
 * vendor-neutral; a provider only has to satisfy the `MarketDataProvider`
 * concept and register a factory. Selecting one is a `MARKET_DATA_PROVIDER`
 * environment variable, so swapping vendors is a redeploy, not a rewrite.
 *
 * `AlpacaProvider` is the implementation that ships. It replaced a Yahoo
 * Finance path that returned a quote and then hardcoded
 * `impliedVolatility = 0.20` (Yahoo's quote endpoint carries no IV) and had no
 * chain support at all — the chain was synthesised in the service layer from a
 * step function.
 *
 * Alpaca supplies the whole grid from three verified endpoints:
 *   spot         GET  {data}/v2/stocks/{sym}/snapshot
 *   chain quotes GET  {data}/v1beta1/options/snapshots/{sym}?feed=opra&...
 *                     -> greeks{delta,gamma,theta,vega,rho}, impliedVolatility,
 *                        latestQuote{ap,bp}, dailyBar{v}
 *   contracts    GET  {trading}/v2/options/contracts?underlying_symbols=...
 *                     -> open_interest, expiration_date, strike_price
 *
 * The two option calls are joined on the OCC symbol: open interest is only on
 * the contracts endpoint, Greeks and IV only on the snapshots endpoint.
 *
 * Credentials come from the environment and nothing else. Live keys
 * authenticate against api.alpaca.markets and paper keys against
 * paper-api.alpaca.markets — mismatching the pair returns 401, which reads
 * like an entitlement problem but is not.
 */
namespace options_calculator::market_data {

export enum class MarketDataError {
    NetworkError = 1,
    HttpError,
    ParseError,
    MissingData,
    NotConfigured,
    NotFound,
    CircuitOpen,
};

[[nodiscard]] auto market_data_category() noexcept -> const std::error_category& {
    struct category_impl : std::error_category {
        [[nodiscard]] auto name() const noexcept -> const char* override { return "market_data"; }
        [[nodiscard]] auto message(int ev) const -> std::string override {
            switch (static_cast<MarketDataError>(ev)) {
                case MarketDataError::NetworkError:  return "Network connection to the market data provider failed";
                case MarketDataError::HttpError:     return "Market data provider returned an error status";
                case MarketDataError::ParseError:    return "Failed to parse the provider response";
                case MarketDataError::MissingData:   return "Provider response is missing a required field";
                case MarketDataError::NotConfigured: return "ALPACA_API_KEY / ALPACA_API_SECRET are not set";
                case MarketDataError::NotFound:      return "No listed contracts for the requested symbol";
                case MarketDataError::CircuitOpen:   return "Provider has failed repeatedly; refusing without a network attempt until the cooldown elapses";
                default:                             return "Unknown market data error";
            }
        }
    };
    static const category_impl impl;
    return impl;
}

export [[nodiscard]] auto make_error_code(MarketDataError e) -> std::error_code {
    return {static_cast<int>(e), market_data_category()};
}

// --------------------------------------------------------------------------
// Configuration
// --------------------------------------------------------------------------

struct Credentials {
    std::string key;
    std::string secret;
    std::string data_host;
    std::string trading_host;

    [[nodiscard]] auto configured() const noexcept -> bool {
        return !key.empty() && !secret.empty();
    }
};

[[nodiscard]] auto env_or(const char* name, std::string fallback) -> std::string {
    const char* raw = std::getenv(name);
    return (raw != nullptr && *raw != '\0') ? std::string{raw} : std::move(fallback);
}

[[nodiscard]] auto env_seconds_or(const char* name, long long fallback_seconds) -> std::chrono::seconds {
    const auto s = env_or(name, "");
    if (s.empty()) return std::chrono::seconds{fallback_seconds};
    try {
        const auto val = std::stoll(s);
        if (val > 0) return std::chrono::seconds{val};
    } catch (...) {}
    return std::chrono::seconds{fallback_seconds};
}

export using ClockFn = std::chrono::steady_clock::time_point (*)();

inline auto default_clock() -> std::chrono::steady_clock::time_point {
    return std::chrono::steady_clock::now();
}

inline std::atomic<ClockFn> g_clock_fn{default_clock};

export auto set_clock_provider(ClockFn fn) -> void {
    g_clock_fn.store(fn != nullptr ? fn : default_clock);
}

export [[nodiscard]] auto clock_now() -> std::chrono::steady_clock::time_point {
    return g_clock_fn.load()();
}

[[nodiscard]] auto credentials() -> const Credentials& {
    static const Credentials creds{
        .key = env_or("ALPACA_API_KEY", ""),
        .secret = env_or("ALPACA_API_SECRET", ""),
        .data_host = env_or("ALPACA_DATA_URL", "https://data.alpaca.markets"),
        .trading_host = env_or("ALPACA_TRADING_URL", "https://api.alpaca.markets"),
    };
    return creds;
}

// --------------------------------------------------------------------------
// HTTP
// --------------------------------------------------------------------------

/**
 * One keep-alive client per (thread, host).
 *
 * A fresh client per call pays a TLS handshake and a DNS lookup every time;
 * with a chain request issuing several calls back to back that dominates the
 * latency budget.
 */
[[nodiscard]] auto client_for(const std::string& host) -> httplib::Client& {
    thread_local std::unordered_map<std::string, std::unique_ptr<httplib::Client>> clients;
    auto it = clients.find(host);
    if (it == clients.end()) {
        auto cli = std::make_unique<httplib::Client>(host);
        cli->set_connection_timeout(3, 0);
        cli->set_read_timeout(10, 0);
        cli->set_keep_alive(true);
        cli->set_follow_location(true);
        it = clients.emplace(host, std::move(cli)).first;
    }
    return *it->second;
}

// --------------------------------------------------------------------------
// Resilience: retry with backoff + a per-host circuit breaker.
//
// Every call in this module that leaves the process is a single unauthenticated
// GET against a third-party host (Alpaca or home.treasury.gov) over the public
// internet — a dropped connection, a DNS hiccup, or a momentary 503 is routine,
// not exceptional, and previously turned into an immediate, permanent failure
// of whatever RPC triggered it. That is the textbook case for a retry: the
// call is read-only (idempotent — retrying it has no side effect to worry
// about) and the failure mode is transient by nature.
//
// The retry math and the circuit-breaker state machine are SGEE's own
// (sgee.runtime.resilience::BackoffPolicy / CircuitBreaker) rather than a
// hand-rolled sleep loop: BackoffPolicy computes the deterministic
// exponential-with-jitter delay schedule and CircuitBreaker tracks
// Closed/Open/HalfOpen transitions from injected failure/success events, so
// this module owns only the orchestration (when to call attempt(), when to
// sleep, when to give up) and none of the policy arithmetic. This is a
// narrower use of SGEE than calculator_service.cpp's: that service models a
// whole multi-step pricing computation as a graph and lets the interpreter
// walk it; there is no external I/O and nothing here to retry. This module
// has exactly one thing worth retrying — the network call — and no multi-step
// workflow to model, so it takes the two resilience primitives directly
// rather than standing up a Builder/ActionRegistry/Interpreter graph for a
// single action. (Separately: the interpreter's own per-node RetryPolicy does
// not honor delay_ms/backoff_multiplier at all — a failed action is retried
// on the very next batch tick, with no injected delay — so it would not have
// produced real backoff even if a graph were built here.)
//
// The circuit breaker exists so that an actual Alpaca/Treasury outage does not
// turn into a retry storm: once a host has failed kCircuitFailureThreshold
// times in a row, every subsequent call is refused immediately (CircuitOpen)
// without attempting the network at all, for kCircuitCooldownMs, after which
// one trial call is allowed through to test recovery. Breaker state is process-
// wide per host (not per thread, not per request) because the thing it is
// protecting — the host's actual availability — is a property of the host, not
// of any one caller.
// --------------------------------------------------------------------------

/** How many times a fetch is attempted in total, including the first try. */
constexpr std::uint32_t kMaxFetchAttempts = 3;
constexpr std::uint64_t kBackoffBaseMs = 200;
constexpr std::uint64_t kBackoffMaxMs = 2000;
constexpr double kBackoffFactor = 2.0;
constexpr std::uint32_t kCircuitFailureThreshold = 5;
constexpr std::uint64_t kCircuitCooldownMs = 30'000;
constexpr std::uint32_t kCircuitHalfOpenTrials = 1;

/**
 * One process-wide monotonic millisecond clock, shared by every call site.
 *
 * CircuitBreaker::allow/on_success/on_failure compare timestamps ACROSS calls
 * (e.g. "has open_cooldown_ms elapsed since the call that tripped the
 * breaker"), so the clock has to share one fixed origin for the life of the
 * process — a clock rebased to "now" on every fetch would make the cooldown
 * math meaningless the moment two calls are involved.
 */
[[nodiscard]] auto steady_now_ms() -> std::uint64_t {
    static const auto epoch = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - epoch).count());
}

/** A circuit breaker plus the mutex serializing access to it. Not copyable or
 *  movable (std::mutex isn't), so the registry below stores these by pointer. */
struct HostBreaker {
    std::mutex mutex;
    sgee::resilience::CircuitBreaker breaker;

    HostBreaker()
        : breaker{sgee::resilience::CircuitBreakerConfig{
              .failure_threshold = kCircuitFailureThreshold,
              .open_cooldown_ms = kCircuitCooldownMs,
              .half_open_max_trials = kCircuitHalfOpenTrials}} {}
};

/** The circuit breaker for one host, created on first use and held for the
 *  life of the process — this is what makes breaker state process-wide. */
[[nodiscard]] auto host_breaker(const std::string& host_key) -> HostBreaker& {
    static std::mutex registry_mutex;
    static std::unordered_map<std::string, std::unique_ptr<HostBreaker>> registry;

    const std::lock_guard lock{registry_mutex};
    auto it = registry.find(host_key);
    if (it == registry.end()) {
        it = registry.emplace(host_key, std::make_unique<HostBreaker>()).first;
    }
    return *it->second;
}

/** The outcome of one fetch attempt: an error code plus whether trying again
 *  is worth it. A transport failure (no response at all) is always worth
 *  retrying; among HTTP statuses, only the ones that describe a transient
 *  condition (429 rate-limited, 5xx server-side) are — a 4xx client error
 *  (401 bad credentials, 404 not found) will fail exactly the same way on
 *  the next attempt, so retrying it would only burn the retry budget and
 *  delay an error that is not going to resolve itself. */
export struct FetchAttemptError {
    MarketDataError code{MarketDataError::NetworkError};
    bool retryable{false};
};

/** One fetch attempt: whatever network I/O `fetch_with_resilience` retries.
 *  Exposed as a callable seam (rather than `fetch_with_resilience` taking a
 *  host/path/headers triple and doing the httplib call itself) so the retry
 *  and circuit-breaker orchestration is unit-testable against a fake attempt
 *  function, without a real socket. */
export using FetchAttempt = std::function<std::expected<std::string, FetchAttemptError>()>;

/**
 * Runs `attempt` up to kMaxFetchAttempts times against `host_key`'s circuit
 * breaker, sleeping between retryable failures on SGEE's exponential-with-
 * full-jitter backoff schedule.
 *
 * `host_key` identifies the breaker to consult and update, NOT necessarily a
 * literal hostname — callers that want independent breakers for what is
 * physically the same host (or a shared breaker across physically different
 * hosts) are free to key it however makes sense for them. This module keys
 * it by the httplib host string, one breaker per (data host, trading host,
 * Treasury host) triple.
 */
export [[nodiscard]] auto fetch_with_resilience(const std::string& host_key,
                                                const FetchAttempt& attempt)
    -> std::expected<std::string, MarketDataError> {
    auto& log = logger::Logger::getInstance();
    auto& guard = host_breaker(host_key);

    static const sgee::resilience::BackoffPolicy backoff{sgee::resilience::BackoffConfig{
        .base_ms = kBackoffBaseMs,
        .factor = kBackoffFactor,
        .max_ms = kBackoffMaxMs,
        .jitter = sgee::resilience::JitterType::Full}};

    // Full jitter needs an RNG source; thread_local so concurrent gRPC
    // handler threads (this service has no worker pool — see
    // calculator_service.cpp's constructor comment) never share one engine.
    thread_local std::mt19937_64 rng{std::random_device{}()};
    auto jitter_rng = [&]() -> double {
        return std::uniform_real_distribution<double>{0.0, 1.0}(rng);
    };

    MarketDataError last_error = MarketDataError::NetworkError;
    for (std::uint32_t attempt_no = 0; attempt_no < kMaxFetchAttempts; ++attempt_no) {
        bool allowed = false;
        {
            const std::lock_guard lock{guard.mutex};
            allowed = guard.breaker.allow(steady_now_ms());
        }
        if (!allowed) {
            log.warn("Circuit open for {}; refusing without a network attempt", host_key);
            return std::unexpected(MarketDataError::CircuitOpen);
        }

        auto result = attempt();
        if (result) {
            const std::lock_guard lock{guard.mutex};
            guard.breaker.on_success(steady_now_ms());
            return std::move(*result);
        }

        last_error = result.error().code;

        // Only a failure that indicates the UPSTREAM is unhealthy may count
        // toward the circuit breaker: a dropped connection/timeout, or an
        // HTTP 429/5xx. A 4xx client error (404 unknown symbol, 401/403 bad
        // credentials, ...) means the provider answered correctly -- the
        // REQUEST was wrong, not the service -- so it must never push this
        // shared, process-wide breaker toward Open. Five anonymous callers
        // requesting five nonexistent symbols must not take market data
        // down for every other caller for the cooldown period; that is a
        // denial-of-service lever wearing a resilience mechanism's clothes,
        // not resilience.
        //
        // `retryable` already encodes exactly this distinction (see
        // FetchAttemptError's doc comment above): it is true precisely for
        // the transient/server-side conditions that are evidence of
        // upstream unhealth, and false for the deterministic client-side
        // ones that are not. Reusing it here, instead of inventing a
        // second predicate, keeps "should this be retried" and "should
        // this count as upstream unhealth" from silently drifting apart --
        // they describe the same underlying question (is this the
        // provider's fault or the request's?) and this codebase's failure
        // taxonomy already answers it once, in get_text().
        //
        // 401/403 deliberately falls on the "do not count" side too, even
        // though it IS our fault (a misconfigured/rotated credential), not
        // the request's. Two reasons: (1) the fix is rotating the
        // credential, not retrying or tripping a breaker -- Alpaca is
        // healthy and correctly rejecting us, so nothing about "upstream is
        // sick" is true here; (2) it already fails fast and loudly on every
        // single call that uses it, via the non-retryable short-circuit
        // below -- tripping the SHARED breaker on top of that would widen
        // the blast radius from "this credential is wrong" to "market data
        // is down for every symbol and every caller", which is a stranger
        // and more damaging way to surface a config error than simply
        // refusing the calls that actually depend on it.
        if (result.error().retryable) {
            const std::lock_guard lock{guard.mutex};
            guard.breaker.on_failure(steady_now_ms());
        }

        const bool have_attempts_left = attempt_no + 1 < kMaxFetchAttempts;
        if (!result.error().retryable || !have_attempts_left) {
            break;
        }

        const auto delay_ms = backoff.delay_for_attempt(attempt_no, jitter_rng);
        log.warn("Fetch attempt {} for {} failed ({}); retrying in {} ms", attempt_no + 1,
                host_key, static_cast<int>(last_error), delay_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
    }
    return std::unexpected(last_error);
}

/**
 * One GET, no vendor assumptions.
 *
 * Split out of get_json because not every feed is Alpaca: the Treasury yield
 * curve is keyless and answers in CSV, so it must not travel a path that
 * refuses without ALPACA_* credentials and then insists on parsing JSON.
 * Reuses client_for() so it inherits the same per-(thread, host) keep-alive
 * client and the same error mapping.
 *
 * Retries transient failures through fetch_with_resilience() (see the
 * "Resilience" section above): a dropped connection or a 429/5xx is retried
 * with backoff; a genuine HTTP client error (401, 404, ...) is not, since
 * asking again would reproduce exactly the same response.
 */
[[nodiscard]] auto get_text(const std::string& host, const std::string& path,
                            const httplib::Headers& headers)
    -> std::expected<std::string, std::error_code> {
    auto& log = logger::Logger::getInstance();

    auto result = fetch_with_resilience(host, [&]() -> std::expected<std::string, FetchAttemptError> {
        auto res = client_for(host).Get(path, headers);
        if (!res) {
            log.error("Network error: {}{} ({})", host, path, httplib::to_string(res.error()));
            return std::unexpected(FetchAttemptError{MarketDataError::NetworkError, /*retryable=*/true});
        }
        if (res->status != 200) {
            log.error("HTTP {} from {}{}: {}", res->status, host, path, res->body.substr(0, 240));
            const bool transient = res->status == 429 || res->status == 500 || res->status == 502 ||
                                   res->status == 503 || res->status == 504;
            return std::unexpected(FetchAttemptError{MarketDataError::HttpError, transient});
        }
        return std::move(res->body);
    });

    if (!result) return std::unexpected(make_error_code(result.error()));
    return std::move(*result);
}

[[nodiscard]] auto get_json(const std::string& host, const std::string& path)
    -> std::expected<fastjson::json_value, std::error_code> {
    const auto& creds = credentials();
    if (!creds.configured()) {
        return std::unexpected(make_error_code(MarketDataError::NotConfigured));
    }

    auto body = get_text(host, path, {
        {"APCA-API-KEY-ID", creds.key},
        {"APCA-API-SECRET-KEY", creds.secret},
        {"Accept", "application/json"},
    });
    if (!body) return std::unexpected(body.error());

    auto parsed = fastjson::parse(*body);
    if (!parsed.has_value()) {
        return std::unexpected(make_error_code(MarketDataError::ParseError));
    }
    return parsed.value();
}

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------

/** Field access that tolerates absence — providers omit rather than null. */
[[nodiscard]] auto num(const fastjson::json_value& v, const std::string& key, double fallback = 0.0) -> double {
    if (!v.is_object() || !v.contains(key)) return fallback;
    const auto& f = v[key];
    if (f.is_null()) return fallback;
    if (f.is_number()) return f.as_float64();
    // Alpaca returns strike_price and multiplier as JSON strings.
    if (f.is_string()) {
        double out{};
        const auto s = f.as_string();
        const auto* first = s.data();
        const auto* last = s.data() + s.size();
        if (std::from_chars(first, last, out).ec == std::errc{}) return out;
    }
    return fallback;
}

[[nodiscard]] auto str(const fastjson::json_value& v, const std::string& key) -> std::string {
    if (!v.is_object() || !v.contains(key)) return {};
    const auto& f = v[key];
    return (f.is_string()) ? std::string{f.as_string()} : std::string{};
}

[[nodiscard]] auto today() -> std::chrono::year_month_day {
    return std::chrono::year_month_day{
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
}

/**
 * Constant-maturity par yields are bond-equivalent yields on a semiannual
 * compounding convention; Black-Scholes discounts continuously. They are not
 * interchangeable — 3.83% published is 3.794% continuous, 3.6 bp — so both
 * numbers travel to the client: the published one is what a user can check
 * against the source, the continuous one is what the engine may price with.
 */
[[nodiscard]] inline auto bey_to_continuous(double bey) noexcept -> double {
    return 2.0 * std::log1p(bey / 2.0);
}

/** "07/29/2026" -> "2026-07-29". The rest of the system speaks ISO dates. */
[[nodiscard]] auto iso_from_us_date(std::string_view mdy) -> std::string {
    if (mdy.size() != 10 || mdy[2] != '/' || mdy[5] != '/') return {};
    return std::string{mdy.substr(6, 4)} + "-" + std::string{mdy.substr(0, 2)} + "-" +
           std::string{mdy.substr(3, 2)};
}

/**
 * The instant a print was obtained, in the one format the wire uses.
 *
 * EXPORTED because calculator_service stamps `fetched_at` on the futures-chain
 * response with it. It was added unexported, which does not fail where it is
 * defined -- it fails at the CALL SITE, as "declaration of 'rfc3339_now' must
 * be imported from module 'market_data' before it is required", and only once
 * something builds that translation unit.
 */
export [[nodiscard]] auto rfc3339_now() -> std::string {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}",
                       std::chrono::floor<std::chrono::seconds>(
                           std::chrono::system_clock::now()));
}

/**
 * Splits one CSV line. Treasury quotes its header labels ("1 Mo", "1.5 Month")
 * and leaves the data cells bare, and no label contains a comma, so a full CSV
 * grammar would be dead weight here. Empty cells are preserved because they
 * carry meaning: a tenor with no print that day must be skipped, not guessed.
 */
[[nodiscard]] auto csv_cells(std::string_view line) -> std::vector<std::string_view> {
    std::vector<std::string_view> out;
    std::size_t pos = 0;
    while (true) {
        const auto comma = line.find(',', pos);
        auto cell = (comma == std::string_view::npos) ? line.substr(pos)
                                                     : line.substr(pos, comma - pos);
        while (!cell.empty() && (cell.front() == '"' || cell.front() == ' ')) cell.remove_prefix(1);
        while (!cell.empty() && (cell.back() == '"' || cell.back() == '\r' || cell.back() == ' ')) {
            cell.remove_suffix(1);
        }
        out.push_back(cell);
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return out;
}

/** "2026-08-21" -> days from today. Negative for past dates. */
[[nodiscard]] auto days_until(const std::string& iso_date) -> int {
    if (iso_date.size() < 10) return 0;
    int y{}, m{}, d{};
    std::from_chars(iso_date.data() + 0, iso_date.data() + 4, y);
    std::from_chars(iso_date.data() + 5, iso_date.data() + 7, m);
    std::from_chars(iso_date.data() + 8, iso_date.data() + 10, d);

    const std::chrono::year_month_day target{
        std::chrono::year{y}, std::chrono::month{static_cast<unsigned>(m)},
        std::chrono::day{static_cast<unsigned>(d)}};
    if (!target.ok()) return 0;

    const auto a = std::chrono::sys_days{target};
    const auto b = std::chrono::sys_days{today()};
    return static_cast<int>((a - b).count());
}

[[nodiscard]] auto expiry_label(const std::string& iso_date, int dte) -> std::string {
    static constexpr std::array<std::string_view, 13> kMonths{
        "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (iso_date.size() < 10) return iso_date;
    int m{}, d{};
    std::from_chars(iso_date.data() + 5, iso_date.data() + 7, m);
    std::from_chars(iso_date.data() + 8, iso_date.data() + 10, d);
    const auto month = (m >= 1 && m <= 12) ? kMonths[static_cast<std::size_t>(m)] : std::string_view{"?"};

    std::string out;
    out.reserve(32);
    out.append(month).append(" ").append(std::to_string(d)).append(", ")
       .append(iso_date.substr(0, 4)).append(" (").append(std::to_string(dte)).append("d");
    if (dte > 365) out.append(" LEAP");
    out.append(")");
    return out;
}

/**
 * OCC symbol -> (strike, is_call).
 *
 * Format is root + YYMMDD + C|P + strike in thousandths, zero padded to 8:
 * "SPY260821C00740000" is a SPY 740 call expiring 2026-08-21. Parsed from the
 * right because the root is variable length.
 */
struct OccParts {
    double strike{0.0};
    bool is_call{true};
    bool valid{false};
};

[[nodiscard]] auto parse_occ(std::string_view sym) -> OccParts {
    OccParts parts;
    if (sym.size() < 15) return parts;

    const auto strike_field = sym.substr(sym.size() - 8);
    const char type_char = sym[sym.size() - 9];
    if (type_char != 'C' && type_char != 'P') return parts;

    long long thousandths{};
    const auto* first = strike_field.data();
    const auto* last = strike_field.data() + strike_field.size();
    if (std::from_chars(first, last, thousandths).ec != std::errc{}) return parts;

    parts.strike = static_cast<double>(thousandths) / 1000.0;
    parts.is_call = (type_char == 'C');
    parts.valid = true;
    return parts;
}

// --------------------------------------------------------------------------
// Public types
// --------------------------------------------------------------------------

export struct Quote {
    std::string symbol;
    double price{0.0};
    double previous_close{0.0};
    std::string timestamp;
};

export struct OptionQuote {
    double bid{0.0};
    double ask{0.0};
    double delta{0.0};
    double gamma{0.0};
    double theta{0.0};
    double vega{0.0};
    double rho{0.0};
    double iv{0.0};
    long long volume{0};
    long long open_interest{0};
};

export struct StrikeRow {
    double strike{0.0};
    OptionQuote call;
    OptionQuote put;
};

export struct Expiration {
    std::string date;
    int days{0};
    std::string label;
};

export struct Chain {
    std::string symbol;
    double spot{0.0};
    std::string selected_expiration;
    std::vector<Expiration> expirations;
    std::vector<StrikeRow> strikes;
    std::string fetched_at;        // RFC3339, when the backend obtained it
};

/** One tenor on the published curve. */
export struct RatePoint {
    std::string tenor;             // "1M", "2M", "3M", "6M", "1Y"
    int days{0};                   // nominal tenor in calendar days
    double rate_bey{0.0};          // as published, decimal: 3.83% -> 0.0383
    double rate_continuous{0.0};   // 2*ln(1 + bey/2), decimal
};

/**
 * The risk-free rate, with its provenance attached.
 *
 * Source-agnostic on purpose: `source` and `as_of_date` are strings that travel
 * with the value, so the client's provenance labelling survives a provider swap
 * without a proto change.
 */
export struct RiskFreeRate {
    double rate{0.0};              // default tenor, continuously compounded
    double rate_published{0.0};    // default tenor, exactly as published
    std::string tenor;             // which tenor the two scalars above are
    std::string as_of_date;        // observation date, "YYYY-MM-DD"
    std::string source;            // provider dataset identifier
    std::string fetched_at;        // RFC3339, when we obtained it
    std::vector<RatePoint> curve;  // short end, ascending by days
};

// --------------------------------------------------------------------------
// Cache
// --------------------------------------------------------------------------

/**
 * Time-boxed cache.
 *
 * Quotes and chain snapshots move constantly and are cached briefly; the
 * contract registry (expirations, open interest) is republished once a day,
 * so it is held far longer. Guarded by a shared_mutex because reads vastly
 * outnumber writes.
 */
export template <typename T>
class TtlCache {
public:
    explicit TtlCache(std::chrono::seconds ttl) noexcept : ttl_{ttl} {}

    [[nodiscard]] auto get(const std::string& key) const -> std::optional<T> {
        const std::shared_lock lock{mutex_};
        const auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;
        if (clock_now() - it->second.stored_at > ttl_) return std::nullopt;
        return it->second.value;
    }

    auto put(const std::string& key, T value) -> void {
        const std::unique_lock lock{mutex_};
        entries_.insert_or_assign(key, Entry{std::move(value), clock_now()});
    }

    auto clear() -> void {
        const std::unique_lock lock{mutex_};
        entries_.clear();
    }

    auto set_ttl(std::chrono::seconds ttl) noexcept -> void {
        const std::unique_lock lock{mutex_};
        ttl_ = ttl;
    }

    [[nodiscard]] auto get_ttl() const noexcept -> std::chrono::seconds {
        const std::shared_lock lock{mutex_};
        return ttl_;
    }

private:
    struct Entry {
        T value;
        std::chrono::steady_clock::time_point stored_at;
    };

    std::chrono::seconds ttl_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

export [[nodiscard]] auto option_chain_max_stale_seconds() -> std::chrono::seconds {
    return env_seconds_or("OPTION_CHAIN_MAX_STALE_SECONDS", 3600);
}

export [[nodiscard]] auto quote_cache() -> TtlCache<Quote>& {
    static TtlCache<Quote> cache{env_seconds_or("OPTION_QUOTE_TTL_SECONDS", 60)};
    return cache;
}

export [[nodiscard]] auto expiration_cache() -> TtlCache<std::vector<Expiration>>& {
    static TtlCache<std::vector<Expiration>> cache{std::chrono::seconds{12 * 60 * 60}};
    return cache;
}

export [[nodiscard]] auto chain_cache() -> TtlCache<Chain>& {
    static TtlCache<Chain> cache{env_seconds_or("OPTION_CHAIN_TTL_SECONDS", 900)};
    return cache;
}

export [[nodiscard]] auto rate_cache() -> TtlCache<RiskFreeRate>& {
    // One publication per business day at about 15:30 ET, so a long TTL costs
    // nothing in freshness: at most four requests a day to the source, and a
    // new print is picked up within hours. The 12 h expiration_cache above is
    // the precedent for this shape.
    static TtlCache<RiskFreeRate> cache{std::chrono::hours{6}};
    return cache;
}

export struct LastGoodChainEntry {
    Chain chain;
    std::chrono::steady_clock::time_point fetched_at_time;
};

[[nodiscard]] auto last_good_chain() -> std::unordered_map<std::string, LastGoodChainEntry>& {
    static std::unordered_map<std::string, LastGoodChainEntry> map;
    return map;
}

[[nodiscard]] auto last_good_chain_mutex() -> std::shared_mutex& {
    static std::shared_mutex mutex;
    return mutex;
}

export auto clear_last_good_chain() -> void {
    const std::unique_lock lock{last_good_chain_mutex()};
    last_good_chain().clear();
}

[[nodiscard]] auto last_good_rate() -> std::optional<RiskFreeRate>& {
    static std::optional<RiskFreeRate> slot;
    return slot;
}

[[nodiscard]] auto last_good_rate_mutex() -> std::shared_mutex& {
    static std::shared_mutex mutex;
    return mutex;
}

// --------------------------------------------------------------------------
// Provider boundary
// --------------------------------------------------------------------------

/**
 * What a market data provider has to supply.
 *
 * A compile-time contract, so a new provider is checked at the point of
 * definition rather than failing at a virtual call. Everything above this
 * boundary — caching, the error taxonomy, the types the gRPC service maps
 * from — is vendor-neutral.
 */
export template <typename P>
concept MarketDataProvider = requires(const P& provider, const std::string& symbol,
                                      const std::string& expiration, double spot) {
    { provider.name() } -> std::convertible_to<std::string_view>;
    { provider.quote(symbol) } -> std::same_as<std::expected<Quote, std::error_code>>;
    { provider.expirations(symbol, spot) }
        -> std::same_as<std::expected<std::vector<Expiration>, std::error_code>>;
    { provider.chain(symbol, expiration, spot) } -> std::same_as<std::expected<Chain, std::error_code>>;
};

/**
 * Runtime face of the same contract.
 *
 * The concept gives compile-time checking; this gives runtime substitution, so
 * the provider can be chosen from configuration without the call sites knowing
 * which one they got.
 */
export class IMarketDataProvider {
public:
    IMarketDataProvider() = default;
    IMarketDataProvider(const IMarketDataProvider&) = delete;
    IMarketDataProvider(IMarketDataProvider&&) = delete;
    auto operator=(const IMarketDataProvider&) -> IMarketDataProvider& = delete;
    auto operator=(IMarketDataProvider&&) -> IMarketDataProvider& = delete;
    virtual ~IMarketDataProvider() = default;

    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
    [[nodiscard]] virtual auto quote(const std::string& symbol) const
        -> std::expected<Quote, std::error_code> = 0;
    [[nodiscard]] virtual auto expirations(const std::string& symbol, double spot) const
        -> std::expected<std::vector<Expiration>, std::error_code> = 0;
    [[nodiscard]] virtual auto chain(const std::string& symbol, const std::string& expiration,
                                     double spot) const
        -> std::expected<Chain, std::error_code> = 0;
};

/** Adapts any type satisfying the concept to the runtime interface. */
export template <MarketDataProvider P>
class ProviderAdapter final : public IMarketDataProvider {
public:
    explicit ProviderAdapter(P provider) noexcept : provider_{std::move(provider)} {}

    [[nodiscard]] auto name() const -> std::string_view override { return provider_.name(); }

    [[nodiscard]] auto quote(const std::string& symbol) const
        -> std::expected<Quote, std::error_code> override {
        return provider_.quote(symbol);
    }

    [[nodiscard]] auto expirations(const std::string& symbol, double spot) const
        -> std::expected<std::vector<Expiration>, std::error_code> override {
        return provider_.expirations(symbol, spot);
    }

    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration,
                             double spot) const -> std::expected<Chain, std::error_code> override {
        return provider_.chain(symbol, expiration, spot);
    }

private:
    P provider_;
};

/**
 * What an interest-rate provider has to supply.
 *
 * Deliberately the same two-layer shape as the market data seam: a concept so a
 * new provider is checked where it is defined, and an interface so the active
 * one can be chosen from configuration without any call site knowing which it
 * got. Nothing above this boundary — the type, the cache, the error taxonomy,
 * the gRPC handler — mentions Treasury.
 */
export template <typename P>
concept RateProvider = requires(const P& provider) {
    { provider.name() } -> std::convertible_to<std::string_view>;
    { provider.risk_free_rate() } -> std::same_as<std::expected<RiskFreeRate, std::error_code>>;
};

export class IRateProvider {
public:
    IRateProvider() = default;
    IRateProvider(const IRateProvider&) = delete;
    IRateProvider(IRateProvider&&) = delete;
    auto operator=(const IRateProvider&) -> IRateProvider& = delete;
    auto operator=(IRateProvider&&) -> IRateProvider& = delete;
    virtual ~IRateProvider() = default;

    [[nodiscard]] virtual auto name() const -> std::string_view = 0;
    [[nodiscard]] virtual auto risk_free_rate() const
        -> std::expected<RiskFreeRate, std::error_code> = 0;
};

/** Adapts any type satisfying the concept to the runtime interface. */
export template <RateProvider P>
class RateProviderAdapter final : public IRateProvider {
public:
    explicit RateProviderAdapter(P provider) noexcept : provider_{std::move(provider)} {}

    [[nodiscard]] auto name() const -> std::string_view override { return provider_.name(); }

    [[nodiscard]] auto risk_free_rate() const
        -> std::expected<RiskFreeRate, std::error_code> override {
        return provider_.risk_free_rate();
    }

private:
    P provider_;
};

// --------------------------------------------------------------------------
// Alpaca
// --------------------------------------------------------------------------

/** Nearest listed strike increment for a price level, used to probe expiries. */
[[nodiscard]] auto probe_strike(double spot) -> double {
    const double increment = (spot >= 100.0) ? 5.0 : 1.0;
    return std::round(spot / increment) * increment;
}

export class AlpacaProvider {
public:
    [[nodiscard]] auto name() const -> std::string_view { return "alpaca"; }

    [[nodiscard]] auto quote(const std::string& symbol) const
        -> std::expected<Quote, std::error_code> {
        auto body = get_json(credentials().data_host, "/v2/stocks/" + symbol + "/snapshot");
        if (!body) return std::unexpected(body.error());

        const auto& root = *body;
        Quote out;
        out.symbol = symbol;

        // Last trade is the truest "current price". Fall back to the daily
        // close when the tape is quiet, but never to an invented number.
        if (root.contains("latestTrade")) {
            out.price = num(root["latestTrade"], "p");
            out.timestamp = str(root["latestTrade"], "t");
        }
        if (out.price <= 0.0 && root.contains("dailyBar")) {
            out.price = num(root["dailyBar"], "c");
            out.timestamp = str(root["dailyBar"], "t");
        }
        if (root.contains("prevDailyBar")) {
            out.previous_close = num(root["prevDailyBar"], "c");
        }

        if (out.price <= 0.0) {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }
        return out;
    }

    /**
     * All listed expirations for a symbol.
     *
     * Enumerating the whole contract registry costs ~14,000 records for SPY. A
     * single at-the-money strike appears in every expiration, so filtering to
     * a narrow strike band returns the same expiration set in ~35 records.
     *
     * `expiration_date_gte` is mandatory: without it Alpaca returns only the
     * nearest handful of expirations (five for SPY), silently truncating the
     * chain to the front week.
     */
    [[nodiscard]] auto expirations(const std::string& symbol, double spot) const
        -> std::expected<std::vector<Expiration>, std::error_code> {
        const auto ymd = today();
        std::string from = std::to_string(static_cast<int>(ymd.year()));
        from += (static_cast<unsigned>(ymd.month()) < 10 ? "-0" : "-") +
                std::to_string(static_cast<unsigned>(ymd.month()));
        from += (static_cast<unsigned>(ymd.day()) < 10 ? "-0" : "-") +
                std::to_string(static_cast<unsigned>(ymd.day()));

        // A band rather than an exact strike: weeklies list $1 increments
        // while monthlies and LEAPs list $5, so one strike can miss the long
        // end of the curve entirely.
        const double centre = probe_strike(spot);
        const std::string path =
            "/v2/options/contracts?underlying_symbols=" + symbol +
            "&type=call" +
            "&strike_price_gte=" + std::to_string(static_cast<long long>(centre - 5.0)) +
            "&strike_price_lte=" + std::to_string(static_cast<long long>(centre + 5.0)) +
            "&expiration_date_gte=" + from +
            "&limit=10000";

        auto body = get_json(credentials().trading_host, path);
        if (!body) return std::unexpected(body.error());

        const auto& root = *body;
        if (!root.contains("option_contracts") || !root["option_contracts"].is_array()) {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }

        std::map<std::string, int> distinct;
        for (const auto& c : root["option_contracts"].as_array()) {
            const auto date = str(c, "expiration_date");
            if (date.empty()) continue;
            distinct.emplace(date, days_until(date));
        }
        if (distinct.empty()) {
            return std::unexpected(make_error_code(MarketDataError::NotFound));
        }

        std::vector<Expiration> out;
        out.reserve(distinct.size());
        for (const auto& [date, dte] : distinct) {
            out.push_back(Expiration{.date = date, .days = dte, .label = expiry_label(date, dte)});
        }
        return out;
    }

    /**
     * The option chain for one expiration: EVERY LISTED STRIKE, unfiltered.
     *
     * This used to ask the provider for a fixed +/-12% of spot, and that hid
     * most of the ladder in the way that hides itself -- a plausible window,
     * centred correctly, so nothing looked broken. Measured 2026-09-02: NVDA
     * at 224.92 for the 2026-12-18 expiry asked for 197..252 and returned 25
     * of the 247 strikes that expiry lists (0.5 .. 460). Someone asking after
     * the $184 strike could not reach it by scrolling, because it was filtered
     * at the QUERY and never entered the response at all.
     *
     * The old justification cited "roughly 14,000 contracts" and a "700-strike
     * ladder". Both are figures for ALL EXPIRATIONS AT ONCE, and neither
     * describes this call, which is scoped to one. Measured cost of the full
     * ladder for a single expiry: SPY 642 snapshots / 381 KB / 1.59 s, NVDA
     * 808 / 439 KB / 1.18 s, both a single page.
     *
     * A narrower band was tried first and REJECTED: sigma*sqrt(T) scaling gave
     * NVDA 73 of 247 strikes, which fixes this report and leaves the next one
     * intact, because any band eventually excludes a strike somebody wants.
     * Deep wings come back with no quote and render as an em dash, which is
     * honest -- a strike that never arrives is indistinguishable from one that
     * does not exist.
     *
     * Every value returned is provider data — a strike with no quote comes
     * back with zeros, which the UI renders as an em dash rather than
     * inventing a price for it.
     */
    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration,
                             double spot) const -> std::expected<Chain, std::error_code> {
        const std::string base =
            "/v1beta1/options/snapshots/" + symbol +
            "?feed=opra" +
            "&expiration_date=" + expiration +
            "&limit=1000";

        // FOLLOW THE PAGE TOKEN. Every expiry measured on 2026-09-02 fitted one
        // page (the largest, NVDA, was 808 against a limit of 1000), but
        // `next_page_token` was never read -- so exceeding it would have
        // dropped the far strikes SILENTLY, which is the same defect the band
        // was causing, one layer down, and is now reachable because the band is
        // gone. A truncated ladder must not look like a complete one.
        std::vector<fastjson::json_value> pages;
        std::string page_token;
        for (int page = 0; page < 20; ++page) {
            const std::string path =
                page_token.empty() ? base : base + "&page_token=" + page_token;
            auto body = get_json(credentials().data_host, path);
            // Only the FIRST page may fail the request. A later failure would
            // otherwise yield a chain missing an arbitrary interior slice,
            // which reads as "those strikes are not listed".
            if (!body) {
                if (pages.empty()) return std::unexpected(body.error());
                break;
            }
            if (!(*body).contains("snapshots") || !(*body)["snapshots"].is_object()) {
                if (pages.empty()) {
                    return std::unexpected(make_error_code(MarketDataError::NotFound));
                }
                break;
            }
            page_token = str(*body, "next_page_token");
            pages.push_back(std::move(*body));
            if (page_token.empty()) break;
        }
        if (pages.empty()) {
            return std::unexpected(make_error_code(MarketDataError::NotFound));
        }

        const auto open_interest = fetch_open_interest(symbol, expiration);

        std::map<double, StrikeRow> rows;
        for (const auto& root : pages) {
        for (const auto& [occ, snap] : root["snapshots"].as_object()) {
            const auto parts = parse_occ(occ);
            if (!parts.valid) continue;

            OptionQuote q;
            if (snap.contains("latestQuote")) {
                q.bid = num(snap["latestQuote"], "bp");
                q.ask = num(snap["latestQuote"], "ap");
            }
            if (snap.contains("greeks")) {
                const auto& g = snap["greeks"];
                q.delta = num(g, "delta");
                q.gamma = num(g, "gamma");
                q.theta = num(g, "theta");
                q.vega  = num(g, "vega");
                q.rho   = num(g, "rho");
            }
            q.iv = num(snap, "impliedVolatility");
            if (snap.contains("dailyBar")) {
                q.volume = static_cast<long long>(num(snap["dailyBar"], "v"));
            }
            if (const auto oi = open_interest.find(occ); oi != open_interest.end()) {
                q.open_interest = oi->second;
            }

            auto& row = rows[parts.strike];
            row.strike = parts.strike;
            (parts.is_call ? row.call : row.put) = q;
        }
        }

        if (rows.empty()) {
            return std::unexpected(make_error_code(MarketDataError::NotFound));
        }

        Chain out;
        out.symbol = symbol;
        out.spot = spot;
        out.selected_expiration = expiration;
        out.strikes.reserve(rows.size());
        for (auto& [strike, row] : rows) {
            out.strikes.push_back(std::move(row));
        }
        return out;
    }

private:
    /** Open interest by OCC symbol. Absent from the snapshots feed. */
    [[nodiscard]] static auto fetch_open_interest(const std::string& symbol,
                                                  const std::string& expiration)
        -> std::unordered_map<std::string, long long> {
        std::unordered_map<std::string, long long> out;

        const std::string path =
            "/v2/options/contracts?underlying_symbols=" + symbol +
            "&expiration_date=" + expiration +
            "&limit=10000";

        auto body = get_json(credentials().trading_host, path);
        if (!body) return out;  // Open interest is enriching, not load-bearing.

        const auto& root = *body;
        if (!root.contains("option_contracts") || !root["option_contracts"].is_array()) return out;

        for (const auto& c : root["option_contracts"].as_array()) {
            const auto occ = str(c, "symbol");
            if (occ.empty()) continue;
            const auto oi = static_cast<long long>(num(c, "open_interest", 0.0));
            if (oi > 0) out.emplace(occ, oi);
        }
        return out;
    }
};

static_assert(MarketDataProvider<AlpacaProvider>,
              "AlpacaProvider must satisfy the MarketDataProvider concept");

// --------------------------------------------------------------------------
// US Treasury
//
// The Daily Treasury Par Yield Curve, keyless, one row per business day,
// published around 15:30 ET. The 3-month constant-maturity bill is the standard
// risk-free proxy for listed equity options: it matches typical 0-60 DTE usage
// and it is the deepest point on the short end. The 1-year overstates the
// discount for short-dated positions by real curve slope, 21 bp on the
// 2026-07-29 print, so it is not the default.
//
// The rejected alternative was fiscaldata's avg_interest_rates, which is
// keyless and works but is the monthly average interest cost of Treasury's
// outstanding debt — an accounting statistic blended over a year of issuance,
// a month stale, and not a market yield. Rejected on correctness.
//
// CSV rather than the XML/OData form of the same data because the only parser
// in this module is fastjson: the CSV is a fixed header plus MM/DD/YYYY rows
// and needs std::from_chars, while XML would need a parser or string scraping.
// --------------------------------------------------------------------------

export class TreasuryParYieldProvider {
public:
    [[nodiscard]] auto name() const -> std::string_view { return "us_treasury"; }

    [[nodiscard]] auto risk_free_rate() const -> std::expected<RiskFreeRate, std::error_code> {
        // No key fields, so no configured() check: that is the whole point of
        // this feed. The host is overridable the same way the Alpaca hosts are.
        const auto host = env_or("TREASURY_RATES_URL", "https://home.treasury.gov");
        const auto year = static_cast<int>(today().year());

        auto body = fetch_year(host, year);

        // The file is year-scoped, so on the first business days of January the
        // new year's file exists with a header and no rows. December's last
        // print is still the most recent real observation, and it carries its
        // own date, so falling back to it reports data rather than nothing.
        if (!body || !has_data_row(*body)) {
            auto previous = fetch_year(host, year - 1);
            if (previous && has_data_row(*previous)) body = std::move(previous);
        }
        if (!body) return std::unexpected(body.error());
        return parse(*body);
    }

private:
    struct TenorColumn {
        std::string_view label;   // exactly as Treasury prints it
        std::string_view tenor;   // what we call it on the wire
        int days;                 // nominal tenor, for later interpolation
    };

    [[nodiscard]] static auto fetch_year(const std::string& host, int year)
        -> std::expected<std::string, std::error_code> {
        const auto y = std::to_string(year);
        const auto path =
            "/resource-center/data-chart-center/interest-rates/daily-treasury-rates.csv/" + y +
            "/all?type=daily_treasury_yield_curve&field_tdr_date_value=" + y + "&_format=csv";
        return get_text(host, path, {{"Accept", "text/csv"}});
    }

    [[nodiscard]] static auto has_data_row(std::string_view csv) noexcept -> bool {
        const auto nl = csv.find('\n');
        return nl != std::string_view::npos && csv.substr(nl + 1).find_first_not_of("\r\n") !=
                                                  std::string_view::npos;
    }

    [[nodiscard]] static auto parse(std::string_view csv)
        -> std::expected<RiskFreeRate, std::error_code> {
        // Column positions are never assumed: Treasury inserted a "1.5 Month"
        // column mid-2023 and shifted every index after it. Look the labels up.
        static constexpr std::array<TenorColumn, 5> kTenors{{
            {"1 Mo", "1M", 30},
            {"2 Mo", "2M", 61},
            {"3 Mo", "3M", 91},
            {"6 Mo", "6M", 182},
            {"1 Yr", "1Y", 365},
        }};
        static constexpr std::string_view kDefaultTenor{"3M"};

        const auto header_end = csv.find('\n');
        if (header_end == std::string_view::npos) {
            return std::unexpected(make_error_code(MarketDataError::ParseError));
        }
        const auto header = csv_cells(csv.substr(0, header_end));

        // Rows are newest first, so the first data row is the latest print.
        auto rest = csv.substr(header_end + 1);
        const auto row_end = rest.find('\n');
        const auto row_line = (row_end == std::string_view::npos) ? rest : rest.substr(0, row_end);
        if (row_line.find_first_not_of("\r") == std::string_view::npos) {
            return std::unexpected(make_error_code(MarketDataError::ParseError));
        }
        const auto row = csv_cells(row_line);

        RiskFreeRate out;
        out.source = "us_treasury_par_yield";
        out.fetched_at = rfc3339_now();
        out.as_of_date = row.empty() ? std::string{} : iso_from_us_date(row[0]);

        for (const auto& [label, tenor, days] : kTenors) {
            const auto it = std::ranges::find(header, label);
            if (it == header.end()) continue;
            const auto idx = static_cast<std::size_t>(std::ranges::distance(header.begin(), it));
            if (idx >= row.size() || row[idx].empty()) continue;

            double pct{};
            const auto* first = row[idx].data();
            const auto* last = row[idx].data() + row[idx].size();
            if (std::from_chars(first, last, pct).ec != std::errc{}) continue;

            const double bey = pct / 100.0;
            out.curve.push_back(RatePoint{.tenor = std::string{tenor},
                                          .days = days,
                                          .rate_bey = bey,
                                          .rate_continuous = bey_to_continuous(bey)});
        }

        // A rate without its observation date is not reportable — the date is
        // what lets the client say which day's print it is showing instead of
        // implying "now". Same for a missing default tenor: there is nothing to
        // substitute for it that would not be an invention.
        const auto def = std::ranges::find_if(
            out.curve, [](const RatePoint& p) noexcept { return p.tenor == kDefaultTenor; });
        if (def == out.curve.end() || out.as_of_date.empty()) {
            return std::unexpected(make_error_code(MarketDataError::MissingData));
        }
        out.tenor = def->tenor;
        out.rate = def->rate_continuous;
        out.rate_published = def->rate_bey;
        return out;
    }
};

static_assert(RateProvider<TreasuryParYieldProvider>,
              "TreasuryParYieldProvider must satisfy the RateProvider concept");

// --------------------------------------------------------------------------
// Provider selection
// --------------------------------------------------------------------------

using ProviderFactory = std::function<std::shared_ptr<IMarketDataProvider>()>;

[[nodiscard]] auto factories() -> std::unordered_map<std::string, ProviderFactory>& {
    static std::unordered_map<std::string, ProviderFactory> registry{
        {"alpaca", [] -> std::shared_ptr<IMarketDataProvider> {
             return std::make_shared<ProviderAdapter<AlpacaProvider>>(AlpacaProvider{});
         }},
    };
    return registry;
}

/**
 * Adds a provider under a name usable in MARKET_DATA_PROVIDER.
 *
 * Call before the first data request; the active provider is resolved once and
 * then held. This is the whole extension point — a new vendor is a type
 * satisfying the concept plus one registration.
 */
export template <MarketDataProvider P>
auto register_provider(std::string name) -> void {
    factories().insert_or_assign(std::move(name), [] -> std::shared_ptr<IMarketDataProvider> {
        return std::make_shared<ProviderAdapter<P>>(P{});
    });
}

[[nodiscard]] auto provider_slot() -> std::shared_ptr<IMarketDataProvider>& {
    static std::shared_ptr<IMarketDataProvider> active;
    return active;
}

[[nodiscard]] auto provider_mutex() -> std::shared_mutex& {
    static std::shared_mutex mutex;
    return mutex;
}

/** Overrides the configured provider, mainly so tests can substitute a fake. */
export auto set_provider(std::shared_ptr<IMarketDataProvider> provider) -> void {
    const std::unique_lock lock{provider_mutex()};
    provider_slot() = std::move(provider);
}

/** The active provider, resolved from MARKET_DATA_PROVIDER on first use. */
export [[nodiscard]] auto provider() -> std::shared_ptr<IMarketDataProvider> {
    {
        const std::shared_lock lock{provider_mutex()};
        if (provider_slot()) return provider_slot();
    }

    const std::unique_lock lock{provider_mutex()};
    if (provider_slot()) return provider_slot();

    const auto requested = env_or("MARKET_DATA_PROVIDER", "alpaca");
    const auto& registry = factories();
    const auto it = registry.find(requested);
    if (it == registry.end()) {
        logger::Logger::getInstance().error(
            "MARKET_DATA_PROVIDER='{}' is not registered; falling back to alpaca", requested);
        provider_slot() = registry.at("alpaca")();
    } else {
        provider_slot() = it->second();
    }
    logger::Logger::getInstance().info("Market data provider: {}", provider_slot()->name());
    return provider_slot();
}

using RateProviderFactory = std::function<std::shared_ptr<IRateProvider>()>;

[[nodiscard]] auto rate_factories() -> std::unordered_map<std::string, RateProviderFactory>& {
    static std::unordered_map<std::string, RateProviderFactory> registry{
        {"us_treasury", [] -> std::shared_ptr<IRateProvider> {
             return std::make_shared<RateProviderAdapter<TreasuryParYieldProvider>>(
                 TreasuryParYieldProvider{});
         }},
    };
    return registry;
}

/**
 * Adds a rate provider under a name usable in RISK_FREE_RATE_PROVIDER. The
 * whole extension point: a FRED or broker feed later is one type satisfying the
 * concept, one registration, and one environment variable — no call site moves,
 * and a keyed provider brings its own credentials struct without touching this.
 */
export template <RateProvider P>
auto register_rate_provider(std::string name) -> void {
    rate_factories().insert_or_assign(std::move(name), [] -> std::shared_ptr<IRateProvider> {
        return std::make_shared<RateProviderAdapter<P>>(P{});
    });
}

[[nodiscard]] auto rate_provider_slot() -> std::shared_ptr<IRateProvider>& {
    static std::shared_ptr<IRateProvider> active;
    return active;
}

[[nodiscard]] auto rate_provider_mutex() -> std::shared_mutex& {
    static std::shared_mutex mutex;
    return mutex;
}

/** Overrides the configured rate provider, mainly so tests can substitute a fake. */
export auto set_rate_provider(std::shared_ptr<IRateProvider> provider) -> void {
    const std::unique_lock lock{rate_provider_mutex()};
    rate_provider_slot() = std::move(provider);
}

/** The active rate provider, resolved from RISK_FREE_RATE_PROVIDER on first use. */
export [[nodiscard]] auto rate_provider() -> std::shared_ptr<IRateProvider> {
    {
        const std::shared_lock lock{rate_provider_mutex()};
        if (rate_provider_slot()) return rate_provider_slot();
    }

    const std::unique_lock lock{rate_provider_mutex()};
    if (rate_provider_slot()) return rate_provider_slot();

    const auto requested = env_or("RISK_FREE_RATE_PROVIDER", "us_treasury");
    const auto& registry = rate_factories();
    const auto it = registry.find(requested);
    if (it == registry.end()) {
        logger::Logger::getInstance().error(
            "RISK_FREE_RATE_PROVIDER='{}' is not registered; falling back to us_treasury",
            requested);
        rate_provider_slot() = registry.at("us_treasury")();
    } else {
        rate_provider_slot() = it->second();
    }
    logger::Logger::getInstance().info("Risk-free rate provider: {}",
                                       rate_provider_slot()->name());
    return rate_provider_slot();
}

// --------------------------------------------------------------------------
// Public API
//
// Caching and the default-expiry policy live here rather than in any provider,
// so every provider inherits them and none can quietly opt out.
// --------------------------------------------------------------------------

export [[nodiscard]] auto fetch_quote(const std::string& symbol)
    -> std::expected<Quote, std::error_code> {
    if (auto hit = quote_cache().get(symbol)) return *hit;

    auto result = provider()->quote(symbol);
    if (!result) return result;

    quote_cache().put(symbol, *result);
    return result;
}

export [[nodiscard]] auto fetch_expirations(const std::string& symbol, double spot)
    -> std::expected<std::vector<Expiration>, std::error_code> {
    if (auto hit = expiration_cache().get(symbol)) return *hit;

    auto result = provider()->expirations(symbol, spot);
    if (!result) return result;

    expiration_cache().put(symbol, *result);
    return result;
}

export [[nodiscard]] auto fetch_chain(const std::string& symbol, const std::string& expiration_in)
    -> std::expected<Chain, std::error_code> {
    std::string cache_key;
    if (!expiration_in.empty()) {
        cache_key = symbol + "|" + expiration_in;
        if (auto hit = chain_cache().get(cache_key)) return *hit;
    }

    auto quote = fetch_quote(symbol);
    std::error_code last_error = make_error_code(MarketDataError::NetworkError);

    if (quote) {
        const double spot = quote->price;
        auto expirations = fetch_expirations(symbol, spot);
        if (expirations) {
            std::string expiration = expiration_in;
            if (expiration.empty()) {
                for (const auto& e : *expirations) {
                    if (e.days >= 7) { expiration = e.date; break; }
                }
                if (expiration.empty() && !expirations->empty()) expiration = expirations->back().date;
            }

            if (!expiration.empty()) {
                cache_key = symbol + "|" + expiration;
                if (expiration_in.empty()) {
                    if (auto hit = chain_cache().get(cache_key)) return *hit;
                }

                auto result = provider()->chain(symbol, expiration, spot);
                if (result) {
                    result->expirations = *expirations;
                    if (result->fetched_at.empty()) {
                        result->fetched_at = rfc3339_now();
                    }
                    chain_cache().put(cache_key, *result);

                    const std::unique_lock lock{last_good_chain_mutex()};
                    last_good_chain()[cache_key] = LastGoodChainEntry{
                        .chain = *result,
                        .fetched_at_time = clock_now()
                    };
                    return result;
                } else {
                    last_error = result.error();
                }
            } else {
                last_error = make_error_code(MarketDataError::NotFound);
            }
        } else {
            last_error = expirations.error();
        }
    } else {
        last_error = quote.error();
    }

    // Upstream failure -- serve-stale from last_good_chain if within max_stale hard cap
    {
        const std::shared_lock lock{last_good_chain_mutex()};
        const auto& lg_map = last_good_chain();

        auto it = lg_map.end();
        if (!cache_key.empty()) {
            it = lg_map.find(cache_key);
        }
        if (it == lg_map.end()) {
            for (auto search_it = lg_map.begin(); search_it != lg_map.end(); ++search_it) {
                if (search_it->first.starts_with(symbol + "|")) {
                    it = search_it;
                    break;
                }
            }
        }

        if (it != lg_map.end()) {
            const auto now = clock_now();
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.fetched_at_time);
            const auto max_stale = option_chain_max_stale_seconds();
            if (age <= max_stale) {
                logger::Logger::getInstance().warn(
                    "Option chain refresh failed ({}); serving stale print from {} (age {}s)",
                    last_error.message(), it->second.chain.fetched_at, age.count());
                return it->second.chain;
            }
            logger::Logger::getInstance().warn(
                "Option chain refresh failed and last good print from {} is stale (age {}s > max {}s); refusing",
                it->second.chain.fetched_at, age.count(), max_stale.count());
        }
    }

    return std::unexpected(last_error);
}

export [[nodiscard]] auto fetch_risk_free_rate() -> std::expected<RiskFreeRate, std::error_code> {
    // The datum is global, not per symbol, so there is exactly one key.
    static const std::string kKey{"latest"};
    if (auto hit = rate_cache().get(kKey)) return *hit;

    auto result = rate_provider()->risk_free_rate();
    if (result) {
        rate_cache().put(kKey, *result);
        const std::unique_lock lock{last_good_rate_mutex()};
        last_good_rate() = *result;
        return result;
    }

    // A transport failure must never become an invented number, but the last
    // print we actually read is still an observation and it carries its own
    // as_of_date, which the client displays — so the user sees exactly which
    // day they are looking at rather than a rate with no date behind it.
    {
        const std::shared_lock lock{last_good_rate_mutex()};
        if (last_good_rate()) {
            logger::Logger::getInstance().warn(
                "Risk-free rate refresh failed ({}); serving the {} print",
                result.error().message(), last_good_rate()->as_of_date);
            return *last_good_rate();
        }
    }
    return result;
}

}  // namespace options_calculator::market_data

namespace std {
template <>
struct is_error_code_enum<options_calculator::market_data::MarketDataError> : true_type {};
}  // namespace std
