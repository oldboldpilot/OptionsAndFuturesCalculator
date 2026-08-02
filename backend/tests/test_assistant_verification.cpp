// @author Olumuyiwa Oluwasanmi
//
// Standalone, proto- and gRPC-free tests for the mandatory GP-ARA
// verification stage over the strategy assistant's output
// (src/modules/assistant_verification.cppm). Deliberately does not stand up
// the LLM, gRPC service, or market-data client: `verify_assistant_params()`
// is the exact boundary `assistant_service.cpp` calls into before a single
// field reaches `calculator.assistant.StrategyParams`, so exercising it
// directly with the five raw fields is a faithful, cheap, offline test of
// the verification logic itself -- what the sibling gRPC/LLM path adds on
// top (JSON parsing, live symbol probing, the model call) is out of this
// file's scope and out of this task's owned files.
//
// Proves both directions per the task brief: every rejection case named in
// the brief actually rejects (with the right Outcome, not just "not
// Proven"), AND a legitimate request still passes end to end through this
// module. A validator that rejects everything is exactly as broken as one
// that accepts everything.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

import assistant_verification;
// Named directly below (sensen::gp_ara::ReasonerErrorCode::Indeterminate) to
// prove the reasoner's error code specifically, not just that it errored --
// assistant_verification.cppm imports this non-exported, so this TU needs
// its own import to spell the qualified name.
import sensen.gp_ara_interfaces;

namespace {

using options_calculator::assistant::verify::AssistantParamsInput;
using options_calculator::assistant::verify::Outcome;
using options_calculator::assistant::verify::ReasonCode;
using options_calculator::assistant::verify::RuleBasedReasoner;
using options_calculator::assistant::verify::VerificationFacts;
using options_calculator::assistant::verify::VerificationVerdict;
using options_calculator::assistant::verify::verify_assistant_params;

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

auto outcome_name(Outcome o) -> const char* {
    switch (o) {
        case Outcome::Proven: return "Proven";
        case Outcome::Unsafe: return "Unsafe";
        case Outcome::Indeterminate: return "Indeterminate";
    }
    return "?";
}

auto expect_outcome(const AssistantParamsInput& in, Outcome expected, const std::string& label)
    -> void {
    const auto v = verify_assistant_params(in);
    check(v.outcome == expected,
          label + " -> expected " + outcome_name(expected) + ", got " + outcome_name(v.outcome) +
              (v.message.empty() ? "" : (" (" + v.message + ")")));
}

}  // namespace

auto main() -> int {
    std::printf("=== GP-ARA assistant verification: direction 1 -- illegitimate input rejects ===\n");

    // symbol=CL (crude oil futures root) with asset_class=EQUITY. CL is also
    // Colgate-Palmolive's real NYSE ticker -- exactly the "plausible but
    // wrong" shape the live market-data probe alone would NOT catch (a real
    // equity quote for "CL" genuinely exists), which is why this static,
    // pre-network check matters.
    expect_outcome({.symbol = "CL", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "symbol=CL claimed as EQUITY (known futures root)");

    // A futures-only strategy (category "Futures", not multi-expiry) with a
    // non-futures asset_class.
    expect_outcome({.symbol = "ES", .asset_class = "EQUITY", .strategy = "futures_long",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "futures_long strategy with asset_class=EQUITY");
    expect_outcome({.symbol = "BTC", .asset_class = "CRYPTO", .strategy = "futures_short",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "futures_short strategy with asset_class=CRYPTO");

    // A calendar spread is BOTH futures-only-shaped (per the task's example
    // wording) and multi_expiry -- check the multi_expiry rule fires
    // regardless of which asset_class accompanies it.
    expect_outcome({.symbol = "ES", .asset_class = "FUTURES", .strategy = "futures_calendar",
                    .expiration_days = 60, .quantity = 1},
                   Outcome::Unsafe, "futures_calendar (multi_expiry) even with matching asset_class");
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "calendar_spread",
                    .expiration_days = 60, .quantity = 1},
                   Outcome::Unsafe, "calendar_spread (multi_expiry, options) rejected regardless of days");

    // An equity/options-only strategy with asset_class=FUTURES.
    expect_outcome({.symbol = "NVDA", .asset_class = "FUTURES", .strategy = "bull_call_spread",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "bull_call_spread (options strategy) with asset_class=FUTURES");

    // expiration_days that cannot exist for a strategy with time value: zero
    // and negative. (assistant_service.cpp's own pre-check floors at 0, i.e.
    // permits 0; this mandatory layer is deliberately stricter.)
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 0, .quantity = 1},
                   Outcome::Unsafe, "expiration_days=0");
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = -5, .quantity = 1},
                   Outcome::Unsafe, "expiration_days=-5");
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 999'999, .quantity = 1},
                   Outcome::Unsafe, "expiration_days absurdly large");

    // quantity <= 0, and absurdly large.
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 0},
                   Outcome::Unsafe, "quantity=0");
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = -1},
                   Outcome::Unsafe, "quantity=-1");
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 5'000'000},
                   Outcome::Unsafe, "quantity absurdly large (5,000,000)");

    // strategy=crack_321. Gated out of the assistant even though it is not in
    // (and, per this test, even if it WERE in) strategy_catalogue.
    expect_outcome({.symbol = "CL", .asset_class = "FUTURES", .strategy = "crack_321",
                    .expiration_days = 45, .quantity = 1},
                   Outcome::Unsafe, "strategy=crack_321");

    // A strategy id the catalogue has never heard of at all (plain
    // hallucination, not even a near-miss).
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "bull_call_ladder_wide",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "wholly unknown strategy id");

    // An asset_class the proto vocabulary does not define.
    expect_outcome({.symbol = "SPY", .asset_class = "STONK", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "asset_class=STONK (unmapped value)");

    std::printf("\n=== GP-ARA assistant verification: direction 2 -- legitimate input passes ===\n");

    // The exact request named in the task brief.
    expect_outcome({.symbol = "SPY", .asset_class = "EQUITY", .strategy = "bull_call_spread",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "SPY / EQUITY / bull_call_spread / 30d / 1 contract");

    // A legitimate futures request, non-multi-expiry, matching category.
    expect_outcome({.symbol = "GC", .asset_class = "FUTURES", .strategy = "futures_long",
                    .expiration_days = 45, .quantity = 3},
                   Outcome::Proven, "GC / FUTURES / futures_long / 45d / 3 contracts");

    // A legitimate crypto options request.
    expect_outcome({.symbol = "BTC", .asset_class = "CRYPTO", .strategy = "long_straddle",
                    .expiration_days = 14, .quantity = 2},
                   Outcome::Proven, "BTC / CRYPTO / long_straddle / 14d / 2 contracts");

    std::printf("\n=== GP-ARA assistant verification: the ES collision is NOT this module's call ===\n");

    // "ES" is genuinely ambiguous (E-mini S&P futures root AND Eversource
    // Energy's equity ticker). This static, pre-network reasoner must not
    // guess -- and per its own design, it does not: neither claim is treated
    // as a contradiction here, because resolving which one is real is live
    // market data's job (assistant_service.cpp::probe_symbol, which is what
    // actually turns an unresolved FUTURES claim into a Clarification and a
    // resolving EQUITY claim into Resolved). Proven here means "internally
    // consistent," not "verified real" -- the live probe still has to run.
    expect_outcome({.symbol = "ES", .asset_class = "EQUITY", .strategy = "bull_call_spread",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "ES claimed as EQUITY (ambiguous root, deferred to live probe)");
    expect_outcome({.symbol = "ES", .asset_class = "FUTURES", .strategy = "futures_long",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "ES claimed as FUTURES (ambiguous root, deferred to live probe)");

    std::printf("\n=== GP-ARA assistant verification: Indeterminate never falls through to acceptance ===\n");

    // Direct reasoner-level proof that an "incomplete" fact set -- the one
    // Indeterminate case this module's own domain can produce (see
    // AssistantParamsDomain::translate()'s final else-branch) -- is refused,
    // never certified safe, by RuleBasedReasoner itself.
    {
        RuleBasedReasoner reasoner;
        RuleBasedReasoner::ContextType ctx;
        VerificationFacts incomplete_facts{
            .violated = false,  // NOT a definite violation --
            .incomplete = true, // -- but the reasoner cannot decide it either.
            .reason = ReasonCode::OutOfScope,
            .detail = "synthetic: category this table has never seen"};
        auto result = reasoner.prove_safety(ctx, incomplete_facts);
        check(!result.has_value(), "Indeterminate fact set: prove_safety returns an error, not a bool");
        if (!result.has_value()) {
            check(result.error().code == sensen::gp_ara::ReasonerErrorCode::Indeterminate,
                  "Indeterminate fact set: error code is Indeterminate, not some other failure");
        }
    }

    // A default-constructed VerificationVerdict (the shape a future bug that
    // forgot to set `outcome` on some exotic early-return path would produce)
    // must fail closed, not open.
    {
        VerificationVerdict default_verdict;
        check(default_verdict.outcome == Outcome::Indeterminate,
              "default-constructed VerificationVerdict is Indeterminate, not Proven (fail-closed default)");
    }

    std::printf("\n=== GP-ARA assistant verification: measured latency ===\n");
    {
        const AssistantParamsInput kSample{.symbol = "SPY",
                                                .asset_class = "EQUITY",
                                                .strategy = "bull_call_spread",
                                                .expiration_days = 30,
                                                .quantity = 1};
        constexpr int kIterations = 200'000;
        // Touch the result so the compiler cannot fold the whole loop away.
        volatile int sink = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            const auto v = verify_assistant_params(kSample);
            sink += static_cast<int>(v.outcome);
        }
        const auto end = std::chrono::steady_clock::now();
        (void)sink;
        const auto total_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        const double ns_per_call = static_cast<double>(total_ns) / static_cast<double>(kIterations);
        std::printf("  %d calls in %lld ns total -> %.1f ns/call (%.4f us/call)\n", kIterations,
                    static_cast<long long>(total_ns), ns_per_call, ns_per_call / 1000.0);
        check(ns_per_call < 50'000.0,
              "verification is cheap relative to ~1.1s generation (< 50us/call)");
    }

    std::printf("\n=== Results: %d checks, %d failed ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
