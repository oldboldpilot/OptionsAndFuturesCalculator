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
// Treasury being reachable, and (with one deliberate exception -- the
// half-open recovery section near the end, which sleeps past the real 30s
// cooldown because fetch_with_resilience's clock is not injectable and
// faking that recovery would prove nothing about the code that actually
// ships) no waiting on the real circuit-breaker cooldown, since the
// interesting behavior (does a retryable failure retry? does a non-retryable
// one NOT retry? does the breaker actually stop calling attempt() once it
// trips? does a non-retryable failure count toward the breaker at all?) is
// otherwise all observable from the fake's call count and
// fetch_with_resilience's return value alone.
//
// Sections added to cover the "non-retryable failures must not trip the
// breaker" defect (see market_data.cppm's fetch_with_resilience, the
// on_failure gate): a plain 404-shaped run, a 401/403-shaped run (documented
// separately even though the FetchAttemptError abstraction makes it
// behaviorally identical to 404 -- see that section's comment), a mixed
// 4xx/5xx interleave proving only the 5xx count toward the threshold, and a
// dedicated 5xx-only run proving genuine upstream unhealth still trips the
// breaker and that the half-open recovery path still works. A "fix" that
// stopped the breaker from ever tripping again would be strictly worse than
// the bug it repairs, so that last property is mandatory, not optional.
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_option_pricing_service.cpp and tests/test_mortgage_verification.cpp
// -- not gtest. The sensen coding policy (backend/sensen/config/cpp_details.txt
// rule 39) forbids external test frameworks project-wide.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <expected>
#include <string>
#include <thread>

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

/**
 * Three independent FetchAttempt factories sharing one call counter, for
 * sections that need to interleave distinct failure shapes (a 404-shaped
 * non-retryable failure, a 500-shaped retryable one, and a plain success)
 * against the SAME host and assert the exact, cumulative number of network
 * attempts made across several fetch_with_resilience calls. Unlike
 * ScriptedAttempt, each factory ALWAYS returns the same outcome on every
 * invocation -- no built-in "recover after N failures" -- because these
 * sections are precisely about what happens when a failure never recovers on
 * its own, only stops being retried past the attempt budget.
 */
class MixedFailureAttempts {
  public:
    [[nodiscard]] auto four_oh_four() -> FetchAttempt {
        return [this]() -> std::expected<std::string, FetchAttemptError> {
            ++calls_;
            return std::unexpected(FetchAttemptError{MarketDataError::HttpError,
                                                      /*retryable=*/false});
        };
    }

    [[nodiscard]] auto five_hundred() -> FetchAttempt {
        return [this]() -> std::expected<std::string, FetchAttemptError> {
            ++calls_;
            return std::unexpected(FetchAttemptError{MarketDataError::HttpError,
                                                      /*retryable=*/true});
        };
    }

    [[nodiscard]] auto legit() -> FetchAttempt {
        return [this]() -> std::expected<std::string, FetchAttemptError> {
            ++calls_;
            return std::string{"legit-response"};
        };
    }

    [[nodiscard]] auto calls() const noexcept -> int { return calls_.load(); }

  private:
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

    section("DEFECT: non-retryable 404s must not trip the circuit breaker");
    {
        // Five separate "unknown symbol" lookups against the same host --
        // exactly the shape of five different anonymous callers each
        // requesting a symbol that does not exist. A 404 means the provider
        // answered correctly: the REQUEST was wrong, not the service. None
        // of these may count as evidence the host itself is unhealthy.
        ScriptedAttempt scripted{/*fail_count=*/5, MarketDataError::HttpError,
                                 /*retryable=*/false, "legit-response"};
        const auto attempt = scripted.as_attempt();
        const std::string host = "host-4xx-must-not-trip";

        for (int i = 1; i <= 5; ++i) {
            auto result = fetch_with_resilience(host, attempt);
            check(!result.has_value() && result.error() == MarketDataError::HttpError,
                  "unknown-symbol lookup " + std::to_string(i) + "/5 fails as a plain HttpError");
        }
        check(scripted.calls() == 5,
              "each of the five 404-shaped lookups made exactly one network attempt");

        // A sixth, unrelated, legitimate request against the SAME host. If
        // the five 404s above wrongly counted toward the breaker, this is
        // refused as CircuitOpen without ever reaching the network -- a
        // site-wide market-data outage caused entirely by five bad symbols,
        // possibly all from one anonymous caller. It must succeed instead.
        auto sixth = fetch_with_resilience(host, attempt);
        check(sixth.has_value() && *sixth == "legit-response",
              "a legitimate 6th request on the same host succeeds -- five unrelated 404s did "
              "not open the breaker");
        check(scripted.calls() == 6,
              "the 6th request actually reached the network, proving the breaker was never "
              "tripped by the 404s");
    }

    section("401/403 (bad credentials) also do not trip the shared breaker");
    {
        // Deliberate decision, not an oversight: see the comment on the
        // on_failure gate in fetch_with_resilience (market_data.cppm). A bad
        // credential is OUR misconfiguration, not upstream sickness -- the
        // fix is rotating the credential, not retrying, and Alpaca is
        // healthy and correctly rejecting us. It already fails loudly on
        // every call that uses it via the existing non-retryable short
        // circuit; tripping the SHARED, process-wide breaker on top of that
        // would take market data down for every other symbol and caller
        // too, which is a stranger and more damaging way to surface "the
        // credential is wrong" than simply refusing the calls that use it.
        //
        // get_text() collapses every non-transient HTTP status (401, 403,
        // 404, and any other 4xx) to the same
        // {code=HttpError, retryable=false} FetchAttemptError before this
        // module ever sees it, so this exercises the identical code path as
        // the 404 section above. It gets its own section anyway because the
        // 401/403 case is a judgement call that deserves to be tested and
        // documented on its own, not merely assumed from "404 looks the
        // same".
        ScriptedAttempt scripted{/*fail_count=*/5, MarketDataError::HttpError,
                                 /*retryable=*/false, "legit-response"};
        const auto attempt = scripted.as_attempt();
        const std::string host = "host-401-403-must-not-trip";

        for (int i = 1; i <= 5; ++i) {
            auto result = fetch_with_resilience(host, attempt);
            check(!result.has_value() && result.error() == MarketDataError::HttpError,
                  "bad-credential attempt " + std::to_string(i) + "/5 fails as a plain HttpError");
        }

        auto sixth = fetch_with_resilience(host, attempt);
        check(sixth.has_value() && *sixth == "legit-response",
              "a legitimate request on the same host still succeeds after five 401/403-shaped "
              "failures -- a bad credential fails loudly on its own calls but does not take "
              "market data down for every other caller");
    }

    section("Mixed sequence: interleaved 4xx and 5xx -- only the 5xx count toward the threshold");
    {
        // Two 404s (never count) interleaved with two "500" calls (each of
        // which, always failing, burns its full 3-attempt retry budget and
        // so contributes 3 failures apiece). The breaker must open exactly
        // when the fifth 5xx-driven failure lands -- 3 from the first 500
        // call plus 2 from the second -- never a moment earlier, and never
        // because of the two 404s sitting in between.
        MixedFailureAttempts mixed;
        const std::string host = "host-mixed-4xx-5xx";

        auto c1 = fetch_with_resilience(host, mixed.four_oh_four());
        check(!c1.has_value() && c1.error() == MarketDataError::HttpError,
              "call 1 (404): fails as a plain HttpError");
        check(mixed.calls() == 1, "call 1: exactly one network attempt");

        auto c2 = fetch_with_resilience(host, mixed.four_oh_four());
        check(!c2.has_value() && c2.error() == MarketDataError::HttpError,
              "call 2 (404): fails as a plain HttpError");
        check(mixed.calls() == 2, "call 2: exactly one more network attempt (2 total)");

        auto c3 = fetch_with_resilience(host, mixed.five_hundred());
        check(!c3.has_value() && c3.error() == MarketDataError::HttpError,
              "call 3 (500, never recovers): exhausts its own retry budget -- still Closed, so "
              "the propagated error is the plain HttpError, not yet CircuitOpen");
        check(mixed.calls() == 5, "call 3: retried the full 3-attempt budget (2+3=5 total)");

        auto c4 = fetch_with_resilience(host, mixed.four_oh_four());
        check(!c4.has_value() && c4.error() == MarketDataError::HttpError,
              "call 4 (404): still just a plain HttpError -- the three 500 failures from call "
              "3 did not combine with this 404 to open the breaker");
        check(mixed.calls() == 6, "call 4: exactly one more network attempt (6 total)");

        auto c5 = fetch_with_resilience(host, mixed.five_hundred());
        check(!c5.has_value() && c5.error() == MarketDataError::CircuitOpen,
              "call 5 (500): the breaker opens mid-call -- 3 failures from call 3 plus 2 more "
              "here is exactly the threshold of 5 -- so the 3rd attempt of this call is "
              "refused as CircuitOpen rather than executed");
        check(mixed.calls() == 8,
              "call 5: only 2 more attempts happened before the now-open breaker suppressed "
              "the 3rd (6+2=8 total)");

        auto c6 = fetch_with_resilience(host, mixed.legit());
        check(!c6.has_value() && c6.error() == MarketDataError::CircuitOpen,
              "call 6: refused immediately as CircuitOpen -- the breaker is open because of "
              "the five 500 failures (3+2), never because of the two interleaved 404s");
        check(mixed.calls() == 8,
              "call 6: attempt() was not called at all -- once opened by real upstream "
              "failures, the breaker still refuses everyone exactly as before this fix");
    }

    section("Genuine upstream unhealth (5 consecutive 5xx) still trips the breaker, and "
            "half-open recovery still works");
    {
        // This is the property that must survive the fix: real server-side
        // failures still accumulate and still open the breaker after
        // kCircuitFailureThreshold of them, refusing a subsequent
        // legitimate request exactly as before. A fix that stopped the
        // breaker from ever tripping would remove the protection entirely
        // -- far worse than the DoS-lever defect it repairs.
        MixedFailureAttempts unhealthy;
        const std::string host = "host-real-5xx-still-trips";

        // kMaxFetchAttempts is 3, so a single always-failing call already
        // contributes 3 failures; a second such call crosses the threshold
        // of 5 partway through (3+2=5).
        auto first = fetch_with_resilience(host, unhealthy.five_hundred());
        check(!first.has_value() && first.error() == MarketDataError::HttpError,
              "call 1 (500): exhausts its retry budget against a still-closed breaker");
        check(unhealthy.calls() == 3, "call 1: attempt() ran all 3 times");

        auto second = fetch_with_resilience(host, unhealthy.five_hundred());
        check(!second.has_value() && second.error() == MarketDataError::CircuitOpen,
              "call 2: the breaker opens mid-call (3+2=5 failures) and the 3rd attempt is "
              "refused as CircuitOpen, not the underlying HttpError -- real unhealth still "
              "trips the breaker after this fix");
        check(unhealthy.calls() == 5,
              "call 2: only 2 more attempts happened (3+2=5) before the now-open breaker "
              "suppressed the 3rd");

        auto refused = fetch_with_resilience(host, unhealthy.legit());
        check(!refused.has_value() && refused.error() == MarketDataError::CircuitOpen,
              "a fresh, otherwise-successful request is refused while the breaker is open -- "
              "the existing protection against a real outage is intact");
        check(unhealthy.calls() == 5, "the refused request never touched the network");

        // Half-open trial: once the cooldown elapses, the breaker must
        // allow exactly one trial through -- the actual recovery mechanism,
        // not merely "opens and stays open forever". fetch_with_resilience
        // has no injectable clock, so this sleeps past the real 30s
        // cooldown (kCircuitCooldownMs in market_data.cppm); faking the
        // wait would prove nothing about the code that actually ships.
        std::this_thread::sleep_for(std::chrono::milliseconds{30'500});

        auto trial = fetch_with_resilience(host, unhealthy.legit());
        check(trial.has_value() && *trial == "legit-response",
              "after the cooldown, the half-open trial is allowed through and succeeds -- the "
              "breaker's recovery path still works after this fix");
        check(unhealthy.calls() == 6, "the trial actually reached the network (6th attempt)");

        // The successful trial closes the breaker (half_open_max_trials is
        // 1), so the very next call must succeed as a normal Closed-state
        // call, not merely as another lucky trial.
        auto after_recovery = fetch_with_resilience(host, unhealthy.legit());
        check(after_recovery.has_value() && *after_recovery == "legit-response",
              "immediately after recovery the breaker is Closed again -- a normal call "
              "succeeds outright");
        check(unhealthy.calls() == 7, "the post-recovery call reached the network normally");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
