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

    std::printf("\n=== GP-ARA assistant verification: genuinely ambiguous roots are NOT this "
                "module's call ===\n");

    // "ES" is genuinely ambiguous (E-mini S&P futures root AND Eversource
    // Energy's equity ticker). This static, pre-network reasoner must not
    // guess -- and per its own design, it does not: neither claim is treated
    // as a contradiction here, because resolving which one is real is either
    // live market data's job (assistant_service.cpp::probe_symbol) or the
    // trader's own words' job (assistant_service.cpp::detect_asset_class_signal,
    // run even earlier). Proven here means "internally consistent," not
    // "verified real" -- one of those two checks still has to run.
    expect_outcome({.symbol = "ES", .asset_class = "EQUITY", .strategy = "bull_call_spread",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "ES claimed as EQUITY (ambiguous root, deferred to live probe)");
    expect_outcome({.symbol = "ES", .asset_class = "FUTURES", .strategy = "futures_long",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "ES claimed as FUTURES (ambiguous root, deferred to live probe)");

    // "CL" is ALSO genuinely ambiguous -- crude oil's futures root AND
    // Colgate-Palmolive's real, currently-listed NYSE ticker (verified
    // against a live equity-ticker lookup, not assumed; see
    // assistant_verification.cppm's kAmbiguousRoots doc comment). Before this
    // change CL+EQUITY was flagged Unsafe here on the assumption that CL
    // claimed as an equity was always a hallucination -- which was itself a
    // latent over-refusal bug: a trader legitimately trading Colgate-Palmolive
    // stock would have been silently refused because CL ALSO happens to be a
    // futures root. Moving CL into kAmbiguousRoots corrects that.
    expect_outcome({.symbol = "CL", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "CL claimed as EQUITY (ambiguous root, deferred to live probe)");
    expect_outcome({.symbol = "CL", .asset_class = "FUTURES", .strategy = "futures_long",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven, "CL claimed as FUTURES (ambiguous root, deferred to live probe)");

    // Roots that merely LOOK like they might collide but do not, checked
    // rather than assumed: NQ Mobile Inc traded as NYSE:NQ but is
    // long delisted/renamed; no live NYSE/NASDAQ ticker "GC" exists today
    // (only the COMEX gold futures continuation symbol); "ZB-A" is a Zions
    // Bancorporation PREFERRED-share class, a different ticker from plain
    // "ZB". None of the three should be treated as ambiguous -- the static
    // contradiction rule must still fire for them exactly as it does for any
    // other non-ambiguous futures root.
    expect_outcome({.symbol = "NQ", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "symbol=NQ claimed as EQUITY (not a genuine ambiguity)");
    expect_outcome({.symbol = "GC", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "symbol=GC claimed as EQUITY (not a genuine ambiguity)");
    expect_outcome({.symbol = "ZB", .asset_class = "EQUITY", .strategy = "long_call",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Unsafe, "symbol=ZB claimed as EQUITY (not a genuine ambiguity)");

    std::printf("\n=== GP-ARA assistant verification: ambiguity signal detection "
                "(assistant_service.cpp's job) ===\n");

    using options_calculator::assistant::verify::AssetClassSignal;
    using options_calculator::assistant::verify::build_ambiguity_clarification;
    using options_calculator::assistant::verify::detect_asset_class_signal;
    using options_calculator::assistant::verify::find_ambiguous_root_info;

    auto expect_signal = [](std::string_view symbol, std::string_view context, AssetClassSignal expected,
                            const std::string& label) {
        const auto got = detect_asset_class_signal(symbol, context);
        const auto name = [](AssetClassSignal s) {
            switch (s) {
                case AssetClassSignal::None: return "None";
                case AssetClassSignal::Futures: return "Futures";
                case AssetClassSignal::Equity: return "Equity";
            }
            return "?";
        };
        check(got == expected, label + " -> expected " + std::string{name(expected)} + ", got " +
                                    std::string{name(got)});
    };

    // The production bug, verbatim: "Long ES, 30 days, 1 contract." must NOT
    // resolve silently. "contract" is present but is explicitly NOT decisive
    // on its own (traders say "1 contract" about options on equities too --
    // see the task brief's own caveat) -- no stronger futures/equity signal
    // appears anywhere in the utterance, so this must stay None (i.e. still
    // ask), not quietly resolve to either side.
    expect_signal("ES", "Long ES, 30 days, 1 contract.", AssetClassSignal::None,
                  "\"Long ES, 30 days, 1 contract.\" -- bare \"contract\" is not decisive");

    // Explicit futures disambiguators, both directions of "honour it, don't
    // ask": the word "futures", "e-mini"/"emini", "front month", and a real
    // contract code (root + delivery-month letter + 2-digit year).
    expect_signal("ES", "Long the ES futures, 30 days, 1 contract.", AssetClassSignal::Futures,
                  "\"ES futures\" is decisive");
    expect_signal("ES", "Buy the e-mini S&P, 30 days out.", AssetClassSignal::Futures,
                  "\"e-mini\" is decisive");
    expect_signal("ES", "Long ES, front month, 1 contract.", AssetClassSignal::Futures,
                  "\"front month\" is decisive");
    expect_signal("ES", "Long ESU26, 1 contract.", AssetClassSignal::Futures,
                  "a contract code (ESU26) is decisive");
    expect_signal("CL", "Short CLZ26 outright.", AssetClassSignal::Futures,
                  "a contract code (CLZ26) is decisive for CL too");

    // Explicit equity disambiguators: "shares"/"stock"/"equity", and the
    // company name itself (a trader naming Eversource or Colgate directly
    // clearly means the stock, not the futures root).
    expect_signal("ES", "Buy 100 shares of ES.", AssetClassSignal::Equity, "\"shares\" is decisive");
    expect_signal("CL", "Long CL stock, 30 days.", AssetClassSignal::Equity, "\"stock\" is decisive");
    expect_signal("ES", "Bullish on Eversource into earnings.", AssetClassSignal::Equity,
                  "naming \"Eversource\" is decisive for ES");
    expect_signal("CL", "Covered call on Colgate-Palmolive.", AssetClassSignal::Equity,
                  "naming \"Colgate\" is decisive for CL");

    // A symbol that is not one of the genuinely ambiguous roots is never
    // routed through any of this -- SPY/NVDA/QQQ (and every other
    // in-distribution symbol) must come back None unconditionally, and
    // `find_ambiguous_root_info` must say so directly, proving the whole
    // mechanism is inert for them regardless of what the utterance says.
    expect_signal("SPY", "Buy SPY futures front month e-mini shares", AssetClassSignal::None,
                  "SPY is not an ambiguous root -- signal detection is a no-op for it");
    check(find_ambiguous_root_info("SPY") == nullptr, "SPY is not in the ambiguous-root table");
    check(find_ambiguous_root_info("NVDA") == nullptr, "NVDA is not in the ambiguous-root table");
    check(find_ambiguous_root_info("QQQ") == nullptr, "QQQ is not in the ambiguous-root table");
    check(find_ambiguous_root_info("ES") != nullptr, "ES IS in the ambiguous-root table");
    check(find_ambiguous_root_info("CL") != nullptr, "CL IS in the ambiguous-root table");

    // The clarification message itself: must name BOTH concrete instruments
    // (never "please clarify") and surface the strategy the model inferred,
    // so a trader can catch the separate, out-of-scope "long_put from 'Long
    // ES'" defect too, in the same round trip.
    {
        const auto question = build_ambiguity_clarification("ES", "long_put");
        check(question.has_value(), "build_ambiguity_clarification(ES, ...) produces a question");
        if (question.has_value()) {
            check(question->find("Eversource") != std::string::npos,
                  "clarification names the equity reading (Eversource)");
            check(question->find("E-mini") != std::string::npos ||
                      question->find("e-mini") != std::string::npos,
                  "clarification names the futures reading (E-mini S&P 500)");
            check(question->find("long_put") != std::string::npos,
                  "clarification surfaces the strategy the model inferred (long_put)");
        }
        check(!build_ambiguity_clarification("SPY", "long_call").has_value(),
              "build_ambiguity_clarification(SPY, ...) is nullopt -- SPY is not ambiguous");
    }

    std::printf("\n=== GP-ARA assistant verification: round trip -- an answered clarification "
                "produces a correct, non-looping FUTURES parse ===\n");

    // Round 1: "Long ES, 30 days, 1 contract." has no decisive signal (proven
    // above) -- assistant_service.cpp returns a Clarification and never
    // reaches this module at all for that turn.
    //
    // Round 2: the trader answers the clarification with one word, "futures".
    // assistant_service.cpp concatenates this new utterance with the prior
    // clarification question and re-runs detect_asset_class_signal -- which
    // must now resolve decisively (closing the loop: a second, indecisive
    // answer would correctly ask again, but a decisive one must not loop).
    expect_signal("ES", "futures", AssetClassSignal::Futures,
                  "round-trip: answering \"futures\" resolves decisively");

    // Once resolved, assistant_service.cpp overwrites asset_class to FUTURES
    // before this module ever sees the input -- so the ONLY thing left for
    // this module to prove is that a SENSIBLE second-round model output
    // (symbol=ES, asset_class=FUTURES, a real futures strategy) is Proven,
    // never re-flagged as unsafe or indeterminate. That is exactly the
    // existing ES-claimed-as-FUTURES case proven above -- this is the same
    // fact, named for the round trip specifically so a reader does not have
    // to infer it.
    expect_outcome({.symbol = "ES", .asset_class = "FUTURES", .strategy = "futures_long",
                    .expiration_days = 30, .quantity = 1},
                   Outcome::Proven,
                   "round trip step 2: ES/FUTURES/futures_long after disambiguation is Proven, not re-asked");

    std::printf("\n=== GP-ARA assistant verification: near-miss strategy name normalisation ===\n");

    using options_calculator::assistant::verify::normalize_strategy_alias;

    // The production bug, verbatim: answering the ES clarification with
    // "futures" makes the model emit `long_futures`, a transposition of the
    // catalogue's real `futures_long`. Must resolve to exactly that id.
    {
        const auto alias = normalize_strategy_alias("long_futures");
        check(alias.has_value() && *alias == "futures_long",
              "normalize_strategy_alias(\"long_futures\") -> \"futures_long\"");
    }

    // A second, independently-derived transposition -- proves this is a
    // general mechanism over the catalogue, not a hand-picked single case.
    {
        const auto alias = normalize_strategy_alias("short_futures");
        check(alias.has_value() && *alias == "futures_short",
              "normalize_strategy_alias(\"short_futures\") -> \"futures_short\"");
    }
    {
        const auto alias = normalize_strategy_alias("put_long");
        check(alias.has_value() && *alias == "long_put",
              "normalize_strategy_alias(\"put_long\") -> \"long_put\"");
    }

    // A real catalogue id must never be "normalised" to something else --
    // this function is only ever consulted for strings the catalogue has
    // ALREADY rejected (see assistant_service.cpp's call site), but proving
    // it is inert on real ids directly guards against a future caller
    // wiring it in before the catalogue check by mistake.
    check(!normalize_strategy_alias("futures_long").has_value(),
          "normalize_strategy_alias(\"futures_long\") -- a REAL id -- is nullopt, not re-mapped");
    check(!normalize_strategy_alias("bull_call_spread").has_value(),
          "normalize_strategy_alias(\"bull_call_spread\") is nullopt (three tokens, not aliased)");

    // A wholly unrelated hallucination is not silently mapped to anything --
    // the conservative default is refusal, never a nearest-neighbour guess.
    check(!normalize_strategy_alias("bull_call_ladder_wide").has_value(),
          "normalize_strategy_alias(\"bull_call_ladder_wide\") is nullopt (not a two-token near miss)");
    check(!normalize_strategy_alias("condor_iron_wide").has_value(),
          "normalize_strategy_alias(\"condor_iron_wide\") is nullopt (three tokens)");

    // crack_321 is excluded from the backend catalogue entirely (see
    // strategy_catalogue.cppm's own header comment), so its transposition
    // must not accidentally alias to anything either.
    check(!normalize_strategy_alias("321_crack").has_value(),
          "normalize_strategy_alias(\"321_crack\") is nullopt -- crack_321 is not in the backend catalogue");

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
