module;
#include <algorithm>
#include <concepts>
#include <functional>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

export module market_data;

import fastjson;
import logger;

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
                default:                             return "Unknown market data error";
            }
        }
    };
    static const category_impl impl;
    return impl;
}

[[nodiscard]] auto make_error_code(MarketDataError e) -> std::error_code {
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

[[nodiscard]] auto get_json(const std::string& host, const std::string& path)
    -> std::expected<fastjson::json_value, std::error_code> {
    auto& log = logger::Logger::getInstance();
    const auto& creds = credentials();
    if (!creds.configured()) {
        return std::unexpected(make_error_code(MarketDataError::NotConfigured));
    }

    auto res = client_for(host).Get(path, {
        {"APCA-API-KEY-ID", creds.key},
        {"APCA-API-SECRET-KEY", creds.secret},
        {"Accept", "application/json"},
    });

    if (!res) {
        log.error("Network error: {}{} ({})", host, path, httplib::to_string(res.error()));
        return std::unexpected(make_error_code(MarketDataError::NetworkError));
    }
    if (res->status != 200) {
        log.error("HTTP {} from {}{}: {}", res->status, host, path, res->body.substr(0, 240));
        return std::unexpected(make_error_code(MarketDataError::HttpError));
    }

    auto parsed = fastjson::parse(res->body);
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
template <typename T>
class TtlCache {
public:
    explicit TtlCache(std::chrono::seconds ttl) noexcept : ttl_{ttl} {}

    [[nodiscard]] auto get(const std::string& key) const -> std::optional<T> {
        const std::shared_lock lock{mutex_};
        const auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() - it->second.stored_at > ttl_) return std::nullopt;
        return it->second.value;
    }

    auto put(const std::string& key, T value) -> void {
        const std::unique_lock lock{mutex_};
        entries_.insert_or_assign(key, Entry{std::move(value), std::chrono::steady_clock::now()});
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

[[nodiscard]] auto quote_cache() -> TtlCache<Quote>& {
    static TtlCache<Quote> cache{std::chrono::seconds{15}};
    return cache;
}

[[nodiscard]] auto expiration_cache() -> TtlCache<std::vector<Expiration>>& {
    static TtlCache<std::vector<Expiration>> cache{std::chrono::seconds{12 * 60 * 60}};
    return cache;
}

[[nodiscard]] auto chain_cache() -> TtlCache<Chain>& {
    static TtlCache<Chain> cache{std::chrono::seconds{15}};
    return cache;
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
     * The option chain for one expiration.
     *
     * Strikes are limited to a window around spot: an unfiltered SPY request
     * is roughly 14,000 contracts at 100 per page, and no trader reads a
     * 700-strike ladder. Every value returned is provider data — a strike with
     * no quote comes back with zeros, which the UI renders as an em dash
     * rather than inventing a price for it.
     */
    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration,
                             double spot) const -> std::expected<Chain, std::error_code> {
        const double band = std::max(spot * 0.12, 5.0);
        const double lo = std::max(1.0, std::floor(spot - band));
        const double hi = std::ceil(spot + band);

        const std::string path =
            "/v1beta1/options/snapshots/" + symbol +
            "?feed=opra" +
            "&expiration_date=" + expiration +
            "&strike_price_gte=" + std::to_string(static_cast<long long>(lo)) +
            "&strike_price_lte=" + std::to_string(static_cast<long long>(hi)) +
            "&limit=1000";

        auto body = get_json(credentials().data_host, path);
        if (!body) return std::unexpected(body.error());

        const auto& root = *body;
        if (!root.contains("snapshots") || !root["snapshots"].is_object()) {
            return std::unexpected(make_error_code(MarketDataError::NotFound));
        }

        const auto open_interest = fetch_open_interest(symbol, expiration, lo, hi);

        std::map<double, StrikeRow> rows;
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
                                                  const std::string& expiration, double lo,
                                                  double hi)
        -> std::unordered_map<std::string, long long> {
        std::unordered_map<std::string, long long> out;

        const std::string path =
            "/v2/options/contracts?underlying_symbols=" + symbol +
            "&expiration_date=" + expiration +
            "&strike_price_gte=" + std::to_string(static_cast<long long>(lo)) +
            "&strike_price_lte=" + std::to_string(static_cast<long long>(hi)) +
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
    auto quote = fetch_quote(symbol);
    if (!quote) return std::unexpected(quote.error());
    const double spot = quote->price;

    auto expirations = fetch_expirations(symbol, spot);
    if (!expirations) return std::unexpected(expirations.error());

    // Default to the first expiry at least a week out — front-week contracts
    // are dominated by gamma and make a poor default view.
    std::string expiration = expiration_in;
    if (expiration.empty()) {
        for (const auto& e : *expirations) {
            if (e.days >= 7) { expiration = e.date; break; }
        }
        if (expiration.empty() && !expirations->empty()) expiration = expirations->back().date;
    }

    const std::string cache_key = symbol + "|" + expiration;
    if (auto hit = chain_cache().get(cache_key)) return *hit;

    auto result = provider()->chain(symbol, expiration, spot);
    if (!result) return result;

    result->expirations = *expirations;
    chain_cache().put(cache_key, *result);
    return result;
}

}  // namespace options_calculator::market_data

namespace std {
template <>
struct is_error_code_enum<options_calculator::market_data::MarketDataError> : true_type {};
}  // namespace std
