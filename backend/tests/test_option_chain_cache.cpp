// @author Olumuyiwa Oluwasanmi
//
// Tests for option-chain caching, TTL expiration, serve-stale resilience on upstream failure,
// and hard-cap refusal in market_data.cppm.

#include <cstdio>


import std;
import market_data;

using namespace options_calculator::market_data;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

/** "2035-08-14" -> sys_days, for a test that reads better with the date spelled out. */
auto sys_days_of(std::string_view iso) -> std::chrono::sys_days {
    int y{}, m{}, d{};
    std::from_chars(iso.data() + 0, iso.data() + 4, y);
    std::from_chars(iso.data() + 5, iso.data() + 7, m);
    std::from_chars(iso.data() + 8, iso.data() + 10, d);
    return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} /
                                 std::chrono::day{static_cast<unsigned>(d)}};
}

/**
 * A fake MarketDataProvider that counts provider chain, quote, and expirations calls,
 * returns distinct payloads per call count, and can be forced to fail on command.
 */
class CountingFakeProvider : public IMarketDataProvider {
public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "counting_fake"; }

    [[nodiscard]] auto quote(const std::string& symbol) const
        -> std::expected<Quote, std::error_code> override {
        if (should_fail_) return std::unexpected(make_error_code(MarketDataError::NetworkError));
        ++quote_calls_;
        return Quote{
            .symbol = symbol,
            .price = 150.0 + static_cast<double>(call_count_),
            .previous_close = 149.9,
            .timestamp = "2026-08-11T14:00:00Z"
        };
    }

    [[nodiscard]] auto expirations(const std::string& symbol, double spot) const
        -> std::expected<std::vector<Expiration>, std::error_code> override {
        if (should_fail_) return std::unexpected(make_error_code(MarketDataError::NetworkError));
        ++expirations_calls_;
        return std::vector<Expiration>{Expiration{.date = "2026-08-21"}};
    }

    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration, double spot) const
        -> std::expected<Chain, std::error_code> override {
        if (should_fail_) return std::unexpected(make_error_code(MarketDataError::NetworkError));
        const std::size_t n = ++chain_calls_;
        ++call_count_;
        Chain c;
        c.symbol = symbol;
        c.spot = spot;
        c.selected_expiration = expiration;
        c.expirations = { Expiration{.date = expiration} };
        c.strikes.push_back(StrikeRow{
            .strike = 150.0,
            .call = OptionQuote{.bid = 2.0 + static_cast<double>(n), .ask = 2.2 + static_cast<double>(n), .delta = 0.5, .iv = 0.2, .volume = 100, .open_interest = 500},
            .put = OptionQuote{.bid = 2.0, .ask = 2.2, .delta = -0.5, .iv = 0.2, .volume = 100, .open_interest = 500}
        });
        return c;
    }

    auto set_should_fail(bool fail) noexcept -> void { should_fail_ = fail; }

    [[nodiscard]] auto chain_calls() const noexcept -> std::size_t { return chain_calls_.load(); }
    [[nodiscard]] auto quote_calls() const noexcept -> std::size_t { return quote_calls_.load(); }
    [[nodiscard]] auto expirations_calls() const noexcept -> std::size_t { return expirations_calls_.load(); }

    auto reset_counts() noexcept -> void {
        chain_calls_ = 0;
        quote_calls_ = 0;
        expirations_calls_ = 0;
        call_count_ = 0;
    }

private:
    mutable std::atomic<bool> should_fail_{false};
    mutable std::atomic<std::size_t> chain_calls_{0};
    mutable std::atomic<std::size_t> quote_calls_{0};
    mutable std::atomic<std::size_t> expirations_calls_{0};
    mutable std::atomic<std::size_t> call_count_{0};
};

/** Two weekly expirations, so a default-expiry choice has something to choose between. */
class TwoExpiryProvider final : public IMarketDataProvider {
public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "two_expiry"; }

    [[nodiscard]] auto quote(const std::string& symbol) const
        -> std::expected<Quote, std::error_code> override {
        return Quote{.symbol = symbol, .price = 150.0, .previous_close = 149.9,
                     .timestamp = "2035-08-07T14:00:00Z"};
    }

    [[nodiscard]] auto expirations(const std::string&, double) const
        -> std::expected<std::vector<Expiration>, std::error_code> override {
        ++expirations_calls_;
        return std::vector<Expiration>{Expiration{.date = "2035-08-14"}, Expiration{.date = "2035-08-21"}};
    }

    [[nodiscard]] auto chain(const std::string& symbol, const std::string& expiration, double spot) const
        -> std::expected<Chain, std::error_code> override {
        Chain c;
        c.symbol = symbol;
        c.spot = spot;
        c.selected_expiration = expiration;
        return c;
    }

    [[nodiscard]] auto expirations_calls() const noexcept -> int { return expirations_calls_.load(); }

private:
    mutable std::atomic<int> expirations_calls_{0};
};

// Fake clock for test time manipulation
std::chrono::steady_clock::time_point g_test_now = std::chrono::steady_clock::now();

auto get_test_now() -> std::chrono::steady_clock::time_point {
    return g_test_now;
}

// Fake calendar: the date the service counts from, crossed by assignment.
std::chrono::sys_days g_test_today{};

auto get_test_today() -> std::chrono::sys_days {
    return g_test_today;
}

} // namespace

auto main() -> int {
    set_clock_provider(get_test_now);

    auto fake = std::make_shared<CountingFakeProvider>();
    set_provider(fake);

    section("1. Cold miss fetches from provider");
    {
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        fake->reset_counts();

        auto res = fetch_chain("AAPL", "2026-08-21");
        check(res.has_value(), "Cold miss successfully returns a chain");
        check(fake->chain_calls() == 1, "Provider chain method was called exactly once on cold miss");
        check(!res->fetched_at.empty(), "fetched_at timestamp is populated on cold fetch");
    }

    section("2. Two fetches inside the TTL produce ONE provider call and identical fetched_at");
    {
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        fake->reset_counts();

        const auto start_time = std::chrono::steady_clock::now();
        g_test_now = start_time;

        auto first = fetch_chain("AAPL", "2026-08-21");
        check(first.has_value(), "First fetch succeeds");
        check(fake->chain_calls() == 1, "First fetch calls provider chain once");
        const std::string first_fetched_at = first->fetched_at;
        check(!first_fetched_at.empty(), "First fetch has non-empty fetched_at");

        // Advance test clock by 5 minutes (well within 15-minute TTL)
        g_test_now = start_time + std::chrono::minutes(5);

        auto second = fetch_chain("AAPL", "2026-08-21");
        check(second.has_value(), "Second fetch inside TTL succeeds");
        check(fake->chain_calls() == 1, "Second fetch inside TTL produces NO additional provider chain calls");
        check(second->fetched_at == first_fetched_at, "Second fetch returns identical fetched_at timestamp");
    }

    section("3. Upstream failure serves last good chain with UNCHANGED fetched_at");
    {
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        fake->reset_counts();

        const auto start_time = std::chrono::steady_clock::now();
        g_test_now = start_time;

        // Cold fetch to populate last good chain and TTL cache
        auto first = fetch_chain("AAPL", "2026-08-21");
        check(first.has_value(), "Initial fetch succeeds");
        const std::string original_fetched_at = first->fetched_at;

        // Advance test clock past 15-minute TTL to 16 minutes
        g_test_now = start_time + std::chrono::minutes(16);

        // Force provider to fail
        fake->set_should_fail(true);

        auto stale_res = fetch_chain("AAPL", "2026-08-21");
        check(stale_res.has_value(), "Response is still served when provider fails within stale cap");
        check(stale_res->fetched_at == original_fetched_at, "fetched_at is UNCHANGED when serving stale print");

        fake->set_should_fail(false);
    }

    section("4. Refuses past the hard cap (1 hour)");
    {
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        fake->reset_counts();

        const auto start_time = std::chrono::steady_clock::now();
        g_test_now = start_time;

        // Initial successful fetch
        auto first = fetch_chain("AAPL", "2026-08-21");
        check(first.has_value(), "Initial fetch succeeds");

        // Advance test clock past hard cap (61 minutes > 60 minute max stale cap)
        g_test_now = start_time + std::chrono::minutes(61);

        // Force provider to fail
        fake->set_should_fail(true);

        auto refused_res = fetch_chain("AAPL", "2026-08-21");
        check(!refused_res.has_value(), "Fetch is refused past the 1-hour hard cap");

        fake->set_should_fail(false);
    }

    section("5. Cold miss with failing provider refuses immediately");
    {
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        fake->reset_counts();

        fake->set_should_fail(true);
        auto cold_fail = fetch_chain("GOOGL", "2026-08-21");
        check(!cold_fail.has_value(), "Cold miss with failing provider returns error");
        fake->set_should_fail(false);
    }

    section("6. A cached expiration is a DATE; its day count is the reader's, against today");
    {
        // The expiration list lives 12 hours, the chain embedding it 15 minutes,
        // the last good chain an hour. Each outlives a UTC midnight, so a count
        // taken at fill time is wrong the morning after.
        // 2035: far from any day this can run on, so a reader that fell back to
        // the real clock would disagree with the injected one.
        const auto day_one = std::chrono::sys_days{std::chrono::year{2035} / std::chrono::October / 5};
        const auto day_two = day_one + std::chrono::days{1};

        check(days_until("2035-10-12", day_one) == 7 && days_until("2035-10-12", day_two) == 6,
              "days_until counts against the day it is handed: 7 on the 5th, 6 on the 6th");
        check(days_until("2035-10-12", sys_days_of("2035-10-12")) == 0 &&
                  days_until("2035-10-12", day_one + std::chrono::days{10}) == -3,
              "an expiry reads 0 on its own day and negative after it");
        check(expiry_label("2035-10-12", 7) == "Oct 12, 2035 (7d)" &&
                  expiry_label("2035-10-12", 6) == "Oct 12, 2035 (6d)",
              "the label prints the count it is given");
        check(expiry_label("2028-10-13", 400) == "Oct 13, 2028 (400d LEAP)",
              "past a year the label says LEAP");
        check(days_until("garbage", day_one) == 0 && days_until("2035-13-45", day_one) == 0,
              "an unparseable date counts as 0 rather than as a number of days from nowhere");

        // The same entry, filled once, read on both days.
        set_calendar_provider(get_test_today);
        expiration_cache().clear();
        g_test_today = day_one;
        expiration_cache().put("SPY", std::vector<Expiration>{Expiration{.date = "2035-10-12"}});

        g_test_today = day_two;
        const auto hit = expiration_cache().get("SPY");
        check(hit.has_value() && hit->size() == 1, "the entry is still cached after midnight");
        check(hit.has_value() && !hit->empty() &&
                  days_until((*hit)[0].date, today()) == 6,
              "...and read through today() it is 6 days away, not the 7 it was when filled");

        // The default-expiry policy counts at READ time too: it picks the first
        // expiration at least 7 days out. The expiration list is cached once and
        // read on two consecutive days; the choice must follow the day.
        auto two = std::make_shared<TwoExpiryProvider>();
        set_provider(two);
        quote_cache().clear();
        chain_cache().clear();
        clear_last_good_chain();
        expiration_cache().clear();

        g_test_today = sys_days_of("2035-08-07");   // the 14th is 7 days out, the 21st 14
        auto on_the_7th = fetch_chain("AAPL", "");
        check(on_the_7th.has_value() && on_the_7th->selected_expiration == "2035-08-14",
              "default expiry: a date exactly 7 days out qualifies");

        g_test_today = sys_days_of("2035-08-08");   // the SAME cached list: the 14th is now 6 out
        chain_cache().clear();
        auto on_the_8th = fetch_chain("AAPL", "");
        check(on_the_8th.has_value() && on_the_8th->selected_expiration == "2035-08-21",
              "default expiry: the next day the cached 14th is 6 days out and is skipped");
        check(two->expirations_calls() == 1,
              "...from the one cached expiration list (the provider was asked for it once)");
        set_provider(fake);
        set_calendar_provider(nullptr);
    }

    // Reset clock provider back to default
    set_clock_provider(nullptr);

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
