// @author Olumuyiwa Oluwasanmi
//
// Tests for market_data.cppm's fetch_with_resilience() -- the retry-with-
// backoff + per-host circuit-breaker orchestration wrapped around get_text()'s
// network fetches. Built on SGEE's own sgee.runtime.resilience::BackoffPolicy
// and CircuitBreaker (see the "Resilience" section of market_data.cppm for
// why those two, and not a hand-rolled loop, are the right primitives here).
//
// This exercises the ORCHESTRATION only, against a fake FetchAttempt supplied
// directly by each section below -- no real socket, no dependence on Alpaca or
// Treasury being reachable, and no waiting on the real (30s) circuit-breaker
// cooldown, since the interesting behavior (does a retryable failure retry?
// does a non-retryable one NOT retry? does the breaker actually stop calling
// attempt() once it trips?) is all observable from the fake's call count and
// fetch_with_resilience's return value alone.
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_option_pricing_service.cpp and tests/test_mortgage_verification.cpp
// -- not gtest. The sensen coding policy (backend/sensen/config/cpp_details.txt
// rule 39) forbids external test frameworks project-wide.
#include <atomic>
#include <cstdio>
#include <expected>
#include <string>

import market_data;

using options_calculator::market_data::FetchAttempt;
using options_calculator::market_data::FetchAttemptError;
using options_calculator::market_data::MarketDataError;
using options_calculator::market_data::fetch_with_resilience;

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

/**
 * A FetchAttempt that fails `fail_count` times (with the given
 * MarketDataError/retryable pair) and then succeeds, returning `success_body`.
 * `fail_count == 0` succeeds on the very first call. Every invocation is
 * counted so a section can assert exactly how many times the network was
 * "hit" -- the whole point of testing the circuit breaker is proving it stops
 * that count from growing once it trips.
 */
class ScriptedAttempt {
  public:
    ScriptedAttempt(int fail_count, MarketDataError fail_code, bool retryable,
                    std::string success_body)
        : fail_count_{fail_count},
          fail_code_{fail_code},
          retryable_{retryable},
          success_body_{std::move(success_body)} {}

    [[nodiscard]] auto as_attempt() -> FetchAttempt {
        return [this]() -> std::expected<std::string, FetchAttemptError> {
            const int n = ++calls_;
            if (n <= fail_count_) {
                return std::unexpected(FetchAttemptError{fail_code_, retryable_});
            }
            return success_body_;
        };
    }

    [[nodiscard]] auto calls() const noexcept -> int { return calls_.load(); }

  private:
    int fail_count_;
    MarketDataError fail_code_;
    bool retryable_;
    std::string success_body_;
    std::atomic<int> calls_{0};
};

}  // namespace

auto main() -> int {
    section("Immediate success -- attempt() called exactly once, no retry");
    {
        ScriptedAttempt scripted{/*fail_count=*/0, MarketDataError::NetworkError,
                                 /*retryable=*/true, "ok"};
        auto result = fetch_with_resilience("host-immediate-success", scripted.as_attempt());
        check(result.has_value(), "fetch_with_resilience succeeds when the first attempt does");
        check(result.has_value() && *result == "ok", "the successful body is returned unchanged");
        check(scripted.calls() == 1, "attempt() was called exactly once (no retry needed)");
    }

    section("Retryable failure recovers within the attempt budget");
    {
        // kMaxFetchAttempts is 3 in market_data.cppm; failing twice and
        // succeeding on the third exercises the retry path without exhausting
        // the budget, so this is the case that actually PROVES retry-then-
        // recover, not merely retry-then-give-up.
        ScriptedAttempt scripted{/*fail_count=*/2, MarketDataError::NetworkError,
                                 /*retryable=*/true, "recovered"};
        auto result = fetch_with_resilience("host-recovers", scripted.as_attempt());
        check(result.has_value(),
              "fetch_with_resilience succeeds once a later attempt does, after retryable "
              "failures");
        check(result.has_value() && *result == "recovered",
              "the body from the eventually-successful attempt is returned");
        check(scripted.calls() == 3,
              "attempt() was retried twice before the third call succeeded (3 total calls)");
    }

    section("Retryable failure exhausts the attempt budget");
    {
        // Always fails, always retryable: this is the "genuine outage" case --
        // the caller should see every attempt actually made, and the final
        // error propagated, not silently swallowed.
        ScriptedAttempt scripted{/*fail_count=*/1000, MarketDataError::NetworkError,
                                 /*retryable=*/true, "unreachable"};
        auto result =
            fetch_with_resilience("host-exhausts-budget", scripted.as_attempt());
        check(!result.has_value(),
              "fetch_with_resilience fails once every attempt in the budget has failed");
        check(!result.has_value() && result.error() == MarketDataError::NetworkError,
              "the propagated error is the underlying failure, not a generic one");
        check(scripted.calls() == 3,
              "attempt() was called exactly kMaxFetchAttempts times, no more and no fewer");
    }

    section("Non-retryable failure short-circuits immediately");
    {
        // A 404/401-shaped failure: FetchAttemptError::retryable == false.
        // Retrying this would reproduce the identical response every time, so
        // fetch_with_resilience must not spend the retry budget on it.
        ScriptedAttempt scripted{/*fail_count=*/1000, MarketDataError::HttpError,
                                 /*retryable=*/false, "unused"};
        auto result =
            fetch_with_resilience("host-non-retryable", scripted.as_attempt());
        check(!result.has_value(), "a non-retryable failure still fails the fetch");
        check(!result.has_value() && result.error() == MarketDataError::HttpError,
              "the propagated error is the non-retryable one, unchanged");
        check(scripted.calls() == 1,
              "attempt() was called exactly once -- a non-retryable failure is NOT retried "
              "even though attempts remained in the budget");
    }

    section("Circuit breaker trips and then refuses without a network attempt");
    {
        // Every call here shares one host key, so breaker state accumulates
        // across the three fetch_with_resilience calls below -- this is
        // deliberately testing the PROCESS-WIDE nature of the breaker, not
        // just one call's internal retry loop.
        ScriptedAttempt scripted{/*fail_count=*/1000, MarketDataError::NetworkError,
                                 /*retryable=*/true, "unused"};
        const auto attempt = scripted.as_attempt();

        // Call 1: all 3 budgeted attempts run (the breaker's
        // kCircuitFailureThreshold is 5, so 3 consecutive failures alone are
        // not enough to trip it). Failures accumulated so far: 3.
        auto first = fetch_with_resilience("host-circuit-breaker", attempt);
        check(!first.has_value() && first.error() == MarketDataError::NetworkError,
              "call 1: exhausts its own retry budget against a still-closed breaker");
        check(scripted.calls() == 3, "call 1: attempt() ran all 3 times");

        // Call 2: the first two attempts push the running failure count from
        // 3 to 5, tripping the breaker open mid-call. The third attempt is
        // therefore refused BEFORE it reaches the network.
        auto second = fetch_with_resilience("host-circuit-breaker", attempt);
        check(!second.has_value() && second.error() == MarketDataError::CircuitOpen,
              "call 2: the breaker opens mid-call and the final attempt is refused as "
              "CircuitOpen rather than the underlying NetworkError");
        check(scripted.calls() == 5,
              "call 2: only 2 more attempt() calls happened (2+3=5 total) -- the 3rd was "
              "suppressed by the now-open breaker, not executed and then discarded");

        // Call 3: a fresh call against the same (still-open, cooldown not
        // elapsed) breaker. This is the check that actually matters: does the
        // breaker prevent a network attempt from being made AT ALL once open,
        // or does it merely relabel a failure that still happened?
        auto third = fetch_with_resilience("host-circuit-breaker", attempt);
        check(!third.has_value() && third.error() == MarketDataError::CircuitOpen,
              "call 3: refused immediately as CircuitOpen");
        check(scripted.calls() == 5,
              "call 3: attempt() was NOT called at all -- the circuit breaker's whole point, "
              "refusing fast without touching the network, is what this call count proves");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
